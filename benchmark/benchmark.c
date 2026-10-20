/*
 * benchmark/benchmark.c
 *
 * EdgeMesh-C load generator and results writer.
 *
 * Usage:
 *   benchmark_client -n <requests> -c <concurrency> -t <url> [-p <policy>]
 *
 *   -n  total requests (default 1000, max 100000)
 *   -c  concurrency   (default 10,   max 256)
 *   -t  target URL    (required; must be http://127.0.0.1:<port>/...)
 *   -p  policy name   (default "round_robin")
 *
 * On completion writes results/latest.json and results/latest.csv,
 * resolved from the executable's own directory (argv[0] via dirname).
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */
#define MAX_REQUESTS    100000
#define MAX_CONCURRENCY 256
#define DEFAULT_N       1000
#define DEFAULT_C       10
#define MAX_URL_LEN     512
#define CONNECT_TIMEOUT_MS 5000
#define READ_TIMEOUT_MS    10000

/* -----------------------------------------------------------------------
 * Shared state
 * ----------------------------------------------------------------------- */
static atomic_int  s_dispatched   = ATOMIC_VAR_INIT(0);
static int         s_total_n      = 0;

/* Per-request result. */
typedef struct {
    int      success;
    double   latency_ms;
    uint64_t bytes;
} req_result_t;

/* Latency samples (filled by workers). */
static double   *s_latencies  = NULL;
static uint64_t *s_bytes_arr  = NULL;
static atomic_int s_completed = ATOMIC_VAR_INIT(0);
static atomic_int s_failed    = ATOMIC_VAR_INIT(0);

/* -----------------------------------------------------------------------
 * Parsed URL
 * ----------------------------------------------------------------------- */
typedef struct {
    char host[64];
    int  port;
    char path[MAX_URL_LEN];
} parsed_url_t;

/* -----------------------------------------------------------------------
 * monotonic_ms helper
 * ----------------------------------------------------------------------- */
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}

/* -----------------------------------------------------------------------
 * rfc3339_now helper
 * ----------------------------------------------------------------------- */
static void fmt_rfc3339(char *buf, size_t len) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_buf;
    gmtime_r(&ts.tv_sec, &tm_buf);
    snprintf(buf, len, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
}

/* -----------------------------------------------------------------------
 * parse_url: only accepts http://127.0.0.1:<port>/<path>
 * ----------------------------------------------------------------------- */
static int parse_url(const char *url, parsed_url_t *out) {
    if (strncmp(url, "http://", 7) != 0) return -1;
    const char *p = url + 7;

    /* Must start with 127.0.0.1 (loopback-only). */
    if (strncmp(p, "127.0.0.1", 9) != 0) return -1;
    p += 9;
    strncpy(out->host, "127.0.0.1", sizeof(out->host) - 1);
    out->host[sizeof(out->host) - 1] = '\0';

    /* Optional port. */
    if (*p == ':') {
        p++;
        char *endp;
        long port = strtol(p, &endp, 10);
        if (port < 1 || port > 65535) return -1;
        out->port = (int)port;
        p = endp;
    } else {
        out->port = 80;
    }

    /* Path (must start with '/'). */
    if (*p == '\0') {
        strncpy(out->path, "/", sizeof(out->path) - 1);
    } else if (*p == '/') {
        strncpy(out->path, p, sizeof(out->path) - 1);
    } else {
        return -1;
    }
    out->path[sizeof(out->path) - 1] = '\0';
    return 0;
}

/* -----------------------------------------------------------------------
 * connect_with_timeout
 * ----------------------------------------------------------------------- */
static int connect_timeout(const char *host, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);
    inet_pton(AF_INET, host, &sa.sin_addr);

    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    if (rc == 0) { fcntl(fd, F_SETFL, flags); return fd; }

    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(fd, &wset);
    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    rc = select(fd + 1, NULL, &wset, NULL, &tv);
    if (rc <= 0) { close(fd); return -1; }

    int err = 0; socklen_t elen = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
    if (err) { close(fd); return -1; }

    fcntl(fd, F_SETFL, flags);
    return fd;
}

/* -----------------------------------------------------------------------
 * do_one_request: perform one HTTP GET, return result.
 * ----------------------------------------------------------------------- */
static req_result_t do_one_request(const parsed_url_t *url) {
    req_result_t r = {0, 0.0, 0};

    uint64_t t0 = now_ms();

    int fd = connect_timeout(url->host, url->port, CONNECT_TIMEOUT_MS);
    if (fd < 0) { r.success = 0; r.latency_ms = (double)(now_ms() - t0); return r; }

    /* Build GET request. */
    char req[1024];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n"
        "\r\n",
        url->path, url->host, url->port);
    if (req_len < 0) req_len = 0;

    /* Send. */
    size_t sent = 0;
    while (sent < (size_t)req_len) {
        ssize_t n = send(fd, req + sent, (size_t)req_len - sent, MSG_NOSIGNAL);
        if (n <= 0) { close(fd); r.latency_ms = (double)(now_ms() - t0); return r; }
        sent += (size_t)n;
    }

    /* Read response. */
    char buf[8192];
    uint64_t bytes = 0;
    int got_status = 0;
    int is_ok      = 0;
    for (;;) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(fd, &rset);
        struct timeval tv;
        tv.tv_sec  = READ_TIMEOUT_MS / 1000;
        tv.tv_usec = (READ_TIMEOUT_MS % 1000) * 1000;
        int rc = select(fd + 1, &rset, NULL, NULL, &tv);
        if (rc <= 0) break;

        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        bytes += (uint64_t)n;

        if (!got_status && bytes >= 12) {
            /* Peek at first line: "HTTP/1.x NNN ..." */
            if (strncmp(buf, "HTTP/", 5) == 0) {
                got_status = 1;
                /* Extract status code: buf[9..11] */
                int code = 0;
                if (bytes >= 12) {
                    code = (buf[9] - '0') * 100 +
                           (buf[10] - '0') * 10 +
                           (buf[11] - '0');
                }
                is_ok = (code >= 200 && code < 300);
            }
        }
    }
    close(fd);

    r.latency_ms = (double)(now_ms() - t0);
    r.success    = (got_status && is_ok) ? 1 : 0;
    r.bytes      = bytes;
    return r;
}

/* -----------------------------------------------------------------------
 * Worker thread
 * ----------------------------------------------------------------------- */
typedef struct {
    const parsed_url_t *url;
    int thread_idx;
} worker_arg_t;

static void *worker_thread(void *arg) {
    const worker_arg_t *wa = (const worker_arg_t *)arg;

    for (;;) {
        /* Atomically claim the next request slot. */
        int slot = atomic_fetch_add(&s_dispatched, 1);
        if (slot >= s_total_n) break;

        req_result_t r = do_one_request(wa->url);

        s_latencies[slot] = r.latency_ms;
        s_bytes_arr[slot] = r.bytes;
        if (r.success) {
            atomic_fetch_add(&s_completed, 1);
        } else {
            atomic_fetch_add(&s_failed, 1);
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Percentile computation (sorts a copy).
 * ----------------------------------------------------------------------- */
static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

static double percentile(double *sorted, int n, double p) {
    if (n == 0) return 0.0;
    double idx  = (p / 100.0) * (double)(n - 1);
    int    lo   = (int)idx;
    double frac = idx - (double)lo;
    if (lo + 1 < n) return sorted[lo] + frac * (sorted[lo + 1] - sorted[lo]);
    return sorted[lo];
}

/* -----------------------------------------------------------------------
 * ensure_results_dir: creates results/ if absent.
 * ----------------------------------------------------------------------- */
static void ensure_results_dir(const char *dir_path) {
    struct stat st;
    if (stat(dir_path, &st) == 0) return; /* already exists */
    mkdir(dir_path, 0755);
}

/* -----------------------------------------------------------------------
 * write_results: writes results/latest.json and results/latest.csv.
 * ----------------------------------------------------------------------- */
static void write_results(const char *exe_dir, const char *url_str,
                          int n_req, int concurrency,
                          int completed, int failed,
                          double duration_s, double rps, double bps,
                          double p50, double p95, double p99,
                          const char *run_id, const char *completed_at,
                          const char *policy) {
    char results_dir[PATH_MAX + 16];
    snprintf(results_dir, sizeof(results_dir), "%s/results", exe_dir);
    ensure_results_dir(results_dir);

    /* ---- JSON ---- */
    char json_path[PATH_MAX + 32];
    snprintf(json_path, sizeof(json_path), "%s/latest.json", results_dir);

    FILE *fp = fopen(json_path, "w");
    if (fp) {
        fprintf(fp,
            "{\n"
            "  \"run_id\": \"%s\",\n"
            "  \"completed_at\": \"%s\",\n"
            "  \"policy\": \"%s\",\n"
            "  \"target\": \"%s\",\n"
            "  \"requested_requests\": %d,\n"
            "  \"concurrency\": %d,\n"
            "  \"completed_requests\": %d,\n"
            "  \"failed_requests\": %d,\n"
            "  \"duration_seconds\": %.3f,\n"
            "  \"requests_per_second\": %.3f,\n"
            "  \"response_bytes_per_second\": %.3f,\n"
            "  \"latency_p50_ms\": %.3f,\n"
            "  \"latency_p95_ms\": %.3f,\n"
            "  \"latency_p99_ms\": %.3f,\n"
            "  \"result_file\": \"results/latest.json\"\n"
            "}\n",
            run_id, completed_at, policy, url_str,
            n_req, concurrency, completed, failed,
            duration_s, rps, bps,
            p50, p95, p99);
        fclose(fp);
        printf("Results written to %s\n", json_path);
    } else {
        fprintf(stderr, "benchmark: could not write %s: %s\n",
                json_path, strerror(errno));
    }

    /* ---- CSV ---- */
    char csv_path[PATH_MAX + 32];
    snprintf(csv_path, sizeof(csv_path), "%s/latest.csv", results_dir);

    fp = fopen(csv_path, "w");
    if (fp) {
        fprintf(fp,
            "run_id,completed_at,policy,target,requested_requests,"
            "concurrency,completed_requests,failed_requests,"
            "duration_seconds,requests_per_second,"
            "response_bytes_per_second,"
            "latency_p50_ms,latency_p95_ms,latency_p99_ms\n");
        fprintf(fp,
            "%s,%s,%s,%s,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
            run_id, completed_at, policy, url_str,
            n_req, concurrency, completed, failed,
            duration_s, rps, bps, p50, p95, p99);
        fclose(fp);
        printf("Results written to %s\n", csv_path);
    }
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    int   n_req       = DEFAULT_N;
    int   concurrency = DEFAULT_C;
    char  url_str[MAX_URL_LEN] = "";
    char  policy_str[64]       = "round_robin";

    /* Capture exe directory for results path. */
    char exe_dir[PATH_MAX] = ".";
    if (argc > 0 && argv[0][0] != '\0') {
        char tmp[PATH_MAX];
        strncpy(tmp, argv[0], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        char *dir = dirname(tmp);
        strncpy(exe_dir, dir, sizeof(exe_dir) - 1);
        exe_dir[sizeof(exe_dir) - 1] = '\0';
    }

    /* Parse arguments. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            char *endp;
            long v = strtol(argv[++i], &endp, 10);
            if (*endp != '\0' || v < 1 || v > MAX_REQUESTS) {
                fprintf(stderr, "benchmark: -n must be 1–%d\n", MAX_REQUESTS);
                return 1;
            }
            n_req = (int)v;
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            char *endp;
            long v = strtol(argv[++i], &endp, 10);
            if (*endp != '\0' || v < 1 || v > MAX_CONCURRENCY) {
                fprintf(stderr, "benchmark: -c must be 1–%d\n", MAX_CONCURRENCY);
                return 1;
            }
            concurrency = (int)v;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            strncpy(url_str, argv[++i], sizeof(url_str) - 1);
            url_str[sizeof(url_str) - 1] = '\0';
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            strncpy(policy_str, argv[++i], sizeof(policy_str) - 1);
            policy_str[sizeof(policy_str) - 1] = '\0';
        } else {
            fprintf(stderr, "benchmark: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "usage: benchmark_client -n <n> -c <c> -t <url> [-p <policy>]\n");
            return 1;
        }
    }

    if (url_str[0] == '\0') {
        fprintf(stderr, "benchmark: -t <url> is required\n");
        fprintf(stderr, "usage: benchmark_client -n <n> -c <c> -t <url> [-p <policy>]\n");
        return 1;
    }

    /* Parse and validate URL (loopback only). */
    parsed_url_t url;
    memset(&url, 0, sizeof(url));
    if (parse_url(url_str, &url) != 0) {
        fprintf(stderr,
            "benchmark: invalid URL '%s'\n"
            "  URL must be http://127.0.0.1:<port>/<path>\n", url_str);
        return 1;
    }

    printf("benchmark_client: n=%d c=%d url=%s\n", n_req, concurrency, url_str);

    /* Allocate per-request result arrays. */
    s_latencies = calloc((size_t)n_req, sizeof(double));
    s_bytes_arr = calloc((size_t)n_req, sizeof(uint64_t));
    if (!s_latencies || !s_bytes_arr) {
        fprintf(stderr, "benchmark: out of memory\n");
        free(s_latencies);
        free(s_bytes_arr);
        return 1;
    }

    s_total_n = n_req;
    atomic_store(&s_dispatched, 0);
    atomic_store(&s_completed,  0);
    atomic_store(&s_failed,     0);

    /* Spawn worker threads. */
    int actual_c = (concurrency < n_req) ? concurrency : n_req;
    pthread_t     *tids = calloc((size_t)actual_c, sizeof(pthread_t));
    worker_arg_t  *wargs = calloc((size_t)actual_c, sizeof(worker_arg_t));
    if (!tids || !wargs) {
        fprintf(stderr, "benchmark: out of memory\n");
        free(tids); free(wargs);
        free(s_latencies); free(s_bytes_arr);
        return 1;
    }

    uint64_t wall_start = now_ms();

    for (int i = 0; i < actual_c; i++) {
        wargs[i].url        = &url;
        wargs[i].thread_idx = i;
        pthread_create(&tids[i], NULL, worker_thread, &wargs[i]);
    }
    for (int i = 0; i < actual_c; i++) {
        pthread_join(tids[i], NULL);
    }

    uint64_t wall_end = now_ms();
    double   duration_s = (double)(wall_end - wall_start) / 1000.0;
    if (duration_s < 0.001) duration_s = 0.001;

    free(tids);
    free(wargs);

    int completed = atomic_load(&s_completed);
    int failed    = atomic_load(&s_failed);

    /* Compute statistics. */
    uint64_t total_bytes = 0;
    for (int i = 0; i < n_req; i++) total_bytes += s_bytes_arr[i];

    double rps = (double)completed / duration_s;
    double bps = (double)total_bytes / duration_s;

    /* Sort latencies for percentiles. */
    double *sorted_lat = malloc((size_t)n_req * sizeof(double));
    if (sorted_lat) {
        memcpy(sorted_lat, s_latencies, (size_t)n_req * sizeof(double));
        qsort(sorted_lat, (size_t)n_req, sizeof(double), cmp_double);
    }

    double p50 = sorted_lat ? percentile(sorted_lat, n_req, 50.0) : 0.0;
    double p95 = sorted_lat ? percentile(sorted_lat, n_req, 95.0) : 0.0;
    double p99 = sorted_lat ? percentile(sorted_lat, n_req, 99.0) : 0.0;
    free(sorted_lat);

    /* Build run_id: bench-YYYYMMDD-HHMMSS */
    time_t now_t = time(NULL);
    struct tm tm_buf;
    gmtime_r(&now_t, &tm_buf);
    char run_id[64];
    snprintf(run_id, sizeof(run_id), "bench-%04d%02d%02d-%02d%02d%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    char completed_at[24];
    fmt_rfc3339(completed_at, sizeof(completed_at));

    /* Print summary. */
    printf("\n--- benchmark results ---\n");
    printf("run_id:           %s\n", run_id);
    printf("completed_at:     %s\n", completed_at);
    printf("policy:           %s\n", policy_str);
    printf("total requests:   %d\n", n_req);
    printf("completed:        %d\n", completed);
    printf("failed:           %d\n", failed);
    printf("duration:         %.3f s\n", duration_s);
    printf("req/s:            %.1f\n", rps);
    printf("bytes/s:          %.0f\n", bps);
    printf("latency p50:      %.1f ms\n", p50);
    printf("latency p95:      %.1f ms\n", p95);
    printf("latency p99:      %.1f ms\n", p99);
    printf("-------------------------\n");

    /* Write results files. */
    write_results(exe_dir, url_str, n_req, concurrency,
                  completed, failed,
                  duration_s, rps, bps,
                  p50, p95, p99,
                  run_id, completed_at,
                  policy_str);

    free(s_latencies);
    free(s_bytes_arr);
    return 0;
}
