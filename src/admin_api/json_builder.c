/*
 * src/admin_api/json_builder.c
 *
 * JSON serialisation helpers for the EdgeMesh-C admin API.
 * All public functions return heap-allocated NUL-terminated strings.
 * The caller must free() the returned pointer.
 */

#define _POSIX_C_SOURCE 200809L

#include "edgemesh/json_builder.h"
#include "edgemesh/backend.h"
#include "edgemesh/metrics.h"
#include "edgemesh/time_utils.h"
#include "edgemesh/log.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Executable path captured at startup by json_builder_init(). */
static char s_exe_dir[PATH_MAX] = "";

/*
 * json_builder_set_exe_path(path)
 *
 * Called from main() so that json_build_benchmark_latest() can resolve
 * results/latest.json relative to the executable's own directory.
 * path is typically argv[0].
 */
void json_builder_set_exe_path(const char *path) {
    if (!path || path[0] == '\0') return;

    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    /* dirname() may modify its argument; work on a copy. */
    char *dir = dirname(tmp);
    strncpy(s_exe_dir, dir, sizeof(s_exe_dir) - 1);
    s_exe_dir[sizeof(s_exe_dir) - 1] = '\0';
}

/* -----------------------------------------------------------------------
 * json_escape_string
 * ----------------------------------------------------------------------- */
void json_escape_string(const char *in, char *out, size_t out_size) {
    if (out_size == 0) return;

    size_t wi = 0; /* write index */

    for (size_t ri = 0; in[ri] != '\0'; ri++) {
        unsigned char c = (unsigned char)in[ri];

        /* Worst case expansion: \uXXXX = 6 chars + NUL */
        if (wi + 7 >= out_size) break;

        if (c == '"') {
            out[wi++] = '\\'; out[wi++] = '"';
        } else if (c == '\\') {
            out[wi++] = '\\'; out[wi++] = '\\';
        } else if (c == '\r') {
            out[wi++] = '\\'; out[wi++] = 'r';
        } else if (c == '\n') {
            out[wi++] = '\\'; out[wi++] = 'n';
        } else if (c == '\t') {
            out[wi++] = '\\'; out[wi++] = 't';
        } else if (c < 0x20) {
            /* Control character: \uXXXX */
            int written = snprintf(out + wi, out_size - wi - 1,
                                   "\\u%04X", (unsigned int)c);
            if (written < 0 || (size_t)written >= out_size - wi - 1) break;
            wi += (size_t)written;
        } else {
            out[wi++] = (char)c;
        }
    }

    out[wi] = '\0';
}

/* -----------------------------------------------------------------------
 * Internal: esc_buf — escaped version of src into a local stack buffer.
 * dst must be at least (2 * strlen(src) + 1) bytes; caller ensures size.
 * ----------------------------------------------------------------------- */
static void esc_into(const char *src, char *dst, size_t dst_size) {
    json_escape_string(src, dst, dst_size);
}

/* -----------------------------------------------------------------------
 * Internal: append helpers used with snprintf-chaining.
 * pos is advanced; if overflow occurs, we keep writing to a /dev/null ptr.
 * ----------------------------------------------------------------------- */
#define BUF_SIZE (65536)  /* 64 KiB working buffer */

/* Append a formatted string to buf[*pos..], NUL-terminated at buf[BUF_SIZE]. */
static void buf_appendf(char *buf, size_t bufsz, size_t *pos,
                        const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void buf_appendf(char *buf, size_t bufsz, size_t *pos,
                        const char *fmt, ...) {
    if (*pos >= bufsz - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *pos, bufsz - *pos, fmt, ap);
    va_end(ap);
    if (n > 0) {
        *pos += (size_t)n;
        if (*pos > bufsz - 1) *pos = bufsz - 1;
    }
}

/* Return a heap copy of buf[0..*pos]. */
static char *heap_copy(const char *buf, size_t pos) {
    char *result = malloc(pos + 1);
    if (!result) return NULL;
    memcpy(result, buf, pos);
    result[pos] = '\0';
    return result;
}

/* -----------------------------------------------------------------------
 * Internal: render a JSON double or "null" for NaN values.
 * ----------------------------------------------------------------------- */
static void buf_append_double_or_null(char *buf, size_t bufsz, size_t *pos,
                                      double val) {
    if (isnan(val) || isinf(val)) {
        buf_appendf(buf, bufsz, pos, "null");
    } else {
        buf_appendf(buf, bufsz, pos, "%.3f", val);
    }
}

/* -----------------------------------------------------------------------
 * Process start time (set on first call to json_build_health()).
 * ----------------------------------------------------------------------- */
static time_t s_start_time = 0;

static void ensure_start_time(void) {
    if (s_start_time == 0) {
        s_start_time = time(NULL);
    }
}

/* -----------------------------------------------------------------------
 * json_build_health
 * ----------------------------------------------------------------------- */
char *json_build_health(void) {
    ensure_start_time();

    char buf[BUF_SIZE];
    size_t pos = 0;

    long uptime = (long)(time(NULL) - s_start_time);
    if (uptime < 0) uptime = 0;

    buf_appendf(buf, sizeof(buf), &pos,
        "{\n"
        "  \"status\": \"ok\",\n"
        "  \"service\": \"edgemesh-c\",\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"uptime_seconds\": %ld\n"
        "}\n",
        uptime);

    return heap_copy(buf, pos);
}

/* -----------------------------------------------------------------------
 * json_build_overview
 * ----------------------------------------------------------------------- */
char *json_build_overview(void) {
    ensure_start_time();

    metrics_t snap = metrics_get_snapshot();

    /* Count backend health states. */
    int healthy = 0, unhealthy = 0, unknown_count = 0;
    pool_lock();
    for (int i = 0; i < BACKEND_POOL_SIZE; i++) {
        switch (g_pool.backends[i].state) {
            case BACKEND_HEALTHY:   healthy++;        break;
            case BACKEND_UNHEALTHY: unhealthy++;      break;
            default:                unknown_count++;  break;
        }
    }
    /* Read policy under policy_mutex. */
    pthread_mutex_lock(&g_pool.policy_mutex);
    lb_policy_t pol = g_pool.policy;
    pthread_mutex_unlock(&g_pool.policy_mutex);
    pool_unlock();

    const char *policy_str = (pol == POLICY_LEAST_CONNECTIONS)
                             ? "least_connections" : "round_robin";

    long uptime = (long)(time(NULL) - s_start_time);
    if (uptime < 0) uptime = 0;

    char ts[24];
    rfc3339_now(ts, sizeof(ts));

    char buf[BUF_SIZE];
    size_t pos = 0;

    buf_appendf(buf, sizeof(buf), &pos,
        "{\n"
        "  \"proxy_running\": true,\n"
        "  \"policy\": \"%s\",\n"
        "  \"uptime_seconds\": %ld,\n"
        "  \"backends\": {\n"
        "    \"total\": %d,\n"
        "    \"healthy\": %d,\n"
        "    \"unhealthy\": %d,\n"
        "    \"unknown\": %d\n"
        "  },\n"
        "  \"metrics\": {\n"
        "    \"requests_total\": %llu,\n"
        "    \"responses_forwarded_total\": %llu,\n"
        "    \"proxy_failures_total\": %llu,\n"
        "    \"requests_per_second\": %.3f,\n"
        "    \"response_bytes_per_second\": %.3f,\n"
        "    \"active_requests\": %d,\n",
        policy_str,
        uptime,
        BACKEND_POOL_SIZE,
        healthy,
        unhealthy,
        unknown_count,
        (unsigned long long)snap.requests_total,
        (unsigned long long)snap.responses_forwarded_total,
        (unsigned long long)snap.proxy_failures_total,
        snap.requests_per_second,
        snap.response_bytes_per_second,
        snap.active_requests);

    buf_appendf(buf, sizeof(buf), &pos, "    \"latency_p50_ms\": ");
    buf_append_double_or_null(buf, sizeof(buf), &pos, snap.p50_ms);
    buf_appendf(buf, sizeof(buf), &pos, ",\n    \"latency_p95_ms\": ");
    buf_append_double_or_null(buf, sizeof(buf), &pos, snap.p95_ms);
    buf_appendf(buf, sizeof(buf), &pos, ",\n    \"latency_p99_ms\": ");
    buf_append_double_or_null(buf, sizeof(buf), &pos, snap.p99_ms);
    buf_appendf(buf, sizeof(buf), &pos,
        "\n  },\n"
        "  \"sample_window_seconds\": 60,\n"
        "  \"generated_at\": \"%s\"\n"
        "}\n",
        ts);

    return heap_copy(buf, pos);
}

/* -----------------------------------------------------------------------
 * json_build_backends
 * ----------------------------------------------------------------------- */
char *json_build_backends(void) {
    char ts[24];
    rfc3339_now(ts, sizeof(ts));

    char buf[BUF_SIZE];
    size_t pos = 0;

    buf_appendf(buf, sizeof(buf), &pos,
        "{\n"
        "  \"backends\": [\n");

    pool_lock();
    for (int i = 0; i < BACKEND_POOL_SIZE; i++) {
        const backend_t *b = &g_pool.backends[i];

        const char *state_str;
        switch (b->state) {
            case BACKEND_HEALTHY:   state_str = "healthy";   break;
            case BACKEND_UNHEALTHY: state_str = "unhealthy"; break;
            default:                state_str = "unknown";   break;
        }

        /* id string: "backend-N" */
        char id_str[32];
        snprintf(id_str, sizeof(id_str), "backend-%d", b->id + 1);

        /* address string: "127.0.0.1:port" */
        char addr_str[80];
        snprintf(addr_str, sizeof(addr_str), "%s:%d", b->address, b->port);

        /* Escape both strings (they are controlled, but be correct). */
        char eid[64], eaddr[96];
        esc_into(id_str,   eid,   sizeof(eid));
        esc_into(addr_str, eaddr, sizeof(eaddr));

        buf_appendf(buf, sizeof(buf), &pos,
            "    {\n"
            "      \"id\": \"%s\",\n"
            "      \"address\": \"%s\",\n"
            "      \"state\": \"%s\",\n"
            "      \"active_connections\": %d,\n"
            "      \"requests_total\": %llu,\n"
            "      \"responses_forwarded_total\": %llu,\n"
            "      \"proxy_failures_total\": %llu,\n",
            eid, eaddr, state_str,
            b->active_connections,
            (unsigned long long)b->requests_total,
            (unsigned long long)b->responses_forwarded,
            (unsigned long long)b->proxy_failures);

        /* last_check_at and last_response_ms may be null before first probe. */
        if (b->last_check_at[0] != '\0') {
            char ets[48];
            esc_into(b->last_check_at, ets, sizeof(ets));
            buf_appendf(buf, sizeof(buf), &pos,
                "      \"last_check_at\": \"%s\",\n"
                "      \"last_response_ms\": %llu\n",
                ets,
                (unsigned long long)b->last_response_ms);
        } else {
            buf_appendf(buf, sizeof(buf), &pos,
                "      \"last_check_at\": null,\n"
                "      \"last_response_ms\": null\n");
        }

        buf_appendf(buf, sizeof(buf), &pos,
            "    }%s\n",
            (i < BACKEND_POOL_SIZE - 1) ? "," : "");
    }
    pool_unlock();

    buf_appendf(buf, sizeof(buf), &pos,
        "  ],\n"
        "  \"generated_at\": \"%s\"\n"
        "}\n",
        ts);

    return heap_copy(buf, pos);
}

/* -----------------------------------------------------------------------
 * json_build_metrics
 * ----------------------------------------------------------------------- */
char *json_build_metrics(int window_s) {
    char ts_now[24];
    rfc3339_now(ts_now, sizeof(ts_now));

    metrics_t snap = metrics_get_snapshot();

    char buf[BUF_SIZE];
    size_t pos = 0;

    buf_appendf(buf, sizeof(buf), &pos,
        "{\n"
        "  \"window_seconds\": %d,\n"
        "  \"points\": [\n",
        window_s);

    /*
     * Emit only time-series points that fall within the requested window.
     * We compare by counting backwards from the newest point.
     */
    int first_point = 0;
    if (snap.series_count > window_s) {
        first_point = snap.series_count - window_s;
    }

    for (int i = first_point; i < snap.series_count; i++) {
        const time_series_point_t *pt = &snap.series[i];

        char ets[48];
        esc_into(pt->timestamp, ets, sizeof(ets));

        buf_appendf(buf, sizeof(buf), &pos,
            "    {\n"
            "      \"timestamp\": \"%s\",\n"
            "      \"requests_total\": %llu,\n"
            "      \"responses_forwarded_total\": %llu,\n"
            "      \"proxy_failures_total\": %llu,\n"
            "      \"requests_per_second\": %.3f,\n"
            "      \"response_bytes_per_second\": %.3f,\n"
            "      \"latency_p50_ms\": null,\n"
            "      \"latency_p95_ms\": null,\n"
            "      \"latency_p99_ms\": null\n"
            "    }%s\n",
            ets,
            (unsigned long long)pt->requests_total,
            (unsigned long long)pt->responses_forwarded_total,
            (unsigned long long)pt->proxy_failures_total,
            pt->requests_per_second,
            pt->response_bytes_per_second,
            (i < snap.series_count - 1) ? "," : "");
    }

    buf_appendf(buf, sizeof(buf), &pos,
        "  ],\n"
        "  \"generated_at\": \"%s\"\n"
        "}\n",
        ts_now);

    return heap_copy(buf, pos);
}

/* -----------------------------------------------------------------------
 * json_build_benchmark_latest
 *
 * Reads ONLY results/latest.json relative to the executable's directory.
 * Never accepts a caller-supplied path.
 * ----------------------------------------------------------------------- */
char *json_build_benchmark_latest(void) {
    /* Build path: <exe_dir>/results/latest.json */
    char path[PATH_MAX + 32];
    if (s_exe_dir[0] != '\0') {
        snprintf(path, sizeof(path), "%s/results/latest.json", s_exe_dir);
    } else {
        /* Fallback: relative path (only used in test contexts). */
        strncpy(path, "results/latest.json", sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }

    /* Try to open the file. */
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT) {
            /* No benchmark run yet — return the "not available" response. */
            const char *not_avail =
                "{\n  \"available\": false,\n  \"run\": null\n}\n";
            size_t n = strlen(not_avail);
            char *result = malloc(n + 1);
            if (!result) return NULL;
            memcpy(result, not_avail, n + 1);
            return result;
        }
        /* Other open error — caller returns 500. */
        log_error("json_build_benchmark_latest: fopen(%s): %s",
                  path, strerror(errno));
        return NULL;
    }

    /* Read up to 64 KiB. */
    char file_buf[BUF_SIZE];
    size_t nread = fread(file_buf, 1, sizeof(file_buf) - 1, fp);
    int read_err = ferror(fp);
    fclose(fp);

    if (read_err) {
        log_error_s("json_build_benchmark_latest: fread error");
        return NULL;
    }

    file_buf[nread] = '\0';

    /* Basic sanity: must start with '{'. */
    size_t i = 0;
    while (i < nread && (file_buf[i] == ' ' || file_buf[i] == '\t' ||
                          file_buf[i] == '\r' || file_buf[i] == '\n')) {
        i++;
    }
    if (i >= nread || file_buf[i] != '{') {
        log_error_s("json_build_benchmark_latest: invalid JSON in results file");
        return NULL;
    }

    /*
     * Wrap the raw file content in the expected envelope.
     * We trust the file was written by our own benchmark tool.
     */
    char buf[BUF_SIZE];
    size_t pos = 0;
    buf_appendf(buf, sizeof(buf), &pos,
        "{\n  \"available\": true,\n  \"run\": %s}\n",
        file_buf);

    return heap_copy(buf, pos);
}
