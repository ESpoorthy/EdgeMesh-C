/*
 * src/proxy/proxy.c
 *
 * Reverse-proxy listener: accepts connections on 127.0.0.1:8080, spawns
 * a detached worker thread per connection (capped at MAX_PROXY_CONNECTIONS),
 * forwards GET requests to a selected backend, returns the response.
 *
 * Engineering guarantees:
 *  - release_backend() is called on EVERY exit path (goto cleanup).
 *  - Pool mutex is NEVER held during network I/O.
 *  - MSG_NOSIGNAL on all send() calls (via write_all).
 *  - Timeout values from architecture.md §5a.
 */

#define _POSIX_C_SOURCE 200809L

#include "edgemesh/proxy.h"
#include "edgemesh/backend.h"
#include "edgemesh/metrics.h"
#include "edgemesh/selector.h"
#include "edgemesh/io_utils.h"
#include "edgemesh/http_parser.h"
#include "edgemesh/http_response.h"
#include "edgemesh/time_utils.h"
#include "edgemesh/log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * Timeout constants (architecture.md §5a)
 * ----------------------------------------------------------------------- */
#define CLIENT_READ_TIMEOUT_MS   (10 * 1000)
#define BACKEND_CONNECT_TIMEOUT_MS (5 * 1000)
#define BACKEND_READ_TIMEOUT_MS  (10 * 1000)
#define OVERALL_DEADLINE_MS      (20 * 1000)

/* -----------------------------------------------------------------------
 * Global state
 * ----------------------------------------------------------------------- */
static int               s_listen_fd   = -1;
static volatile int      s_stop        = 0;
static pthread_mutex_t   s_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static int               s_active_count = 0;
static backend_pool_t   *s_pool        = NULL;
static metrics_t        *s_metrics_ptr = NULL; /* unused direct pointer;
                                                  global g_metrics used */

/* Forward declarations. */
static void *proxy_worker_thread(void *arg);

/* -----------------------------------------------------------------------
 * Worker argument structure
 * ----------------------------------------------------------------------- */
typedef struct {
    int    client_fd;
    char   client_ip[INET_ADDRSTRLEN];
} proxy_worker_args_t;

/* -----------------------------------------------------------------------
 * Internal: non-blocking connect with select()-based timeout.
 * Returns the connected fd (>= 0) or -1 on error/timeout.
 * ----------------------------------------------------------------------- */
static int connect_with_timeout(const char *addr, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    /* Set non-blocking. */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
    if (rc == 0) {
        /* Connected immediately — restore blocking. */
        fcntl(fd, F_SETFL, flags);
        return fd;
    }
    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    /* Wait for writable (= connected or error). */
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(fd, &wset);
    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    rc = select(fd + 1, NULL, &wset, NULL, &tv);
    if (rc <= 0) {
        close(fd);
        return -1; /* timeout or error */
    }

    /* Check if the connection actually succeeded. */
    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
    if (err != 0) {
        close(fd);
        return -1;
    }

    /* Restore blocking. */
    fcntl(fd, F_SETFL, flags);
    return fd;
}

/* -----------------------------------------------------------------------
 * Internal: read backend response headers, then forward the full response.
 * Returns bytes forwarded (>= 0) or -1 on error.
 * ----------------------------------------------------------------------- */
static ssize_t forward_response(int client_fd, int backend_fd) {
    /* Read backend response headers (bounded 16 KiB). */
    char resp_buf[16384];
    ssize_t hlen = read_with_timeout(backend_fd, resp_buf, sizeof(resp_buf),
                                     BACKEND_READ_TIMEOUT_MS);
    if (hlen <= 0) {
        return -1;
    }

    /* Validate: must start with "HTTP/". */
    if (hlen < 5 || strncmp(resp_buf, "HTTP/", 5) != 0) {
        return -1;
    }

    /* Forward header section. */
    ssize_t fw = write_all(client_fd, resp_buf, (size_t)hlen);
    if (fw < 0) return -1;
    ssize_t total_bytes = fw;

    /* Forward remaining body in streaming chunks. */
    char chunk[8192];
    for (;;) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(backend_fd, &rset);
        struct timeval tv;
        tv.tv_sec  = BACKEND_READ_TIMEOUT_MS / 1000;
        tv.tv_usec = 0;
        int rc = select(backend_fd + 1, &rset, NULL, NULL, &tv);
        if (rc < 0)  break; /* error  */
        if (rc == 0) break; /* timeout — stop forwarding */

        ssize_t n = read(backend_fd, chunk, sizeof(chunk));
        if (n <= 0) break; /* EOF or error */

        ssize_t wn = write_all(client_fd, chunk, (size_t)n);
        if (wn < 0) break;
        total_bytes += wn;
    }

    return total_bytes;
}

/* -----------------------------------------------------------------------
 * proxy_worker_thread
 * ----------------------------------------------------------------------- */
static void *proxy_worker_thread(void *arg) {
    proxy_worker_args_t *args = (proxy_worker_args_t *)arg;
    int    client_fd   = args->client_fd;
    char   client_ip[INET_ADDRSTRLEN];
    strncpy(client_ip, args->client_ip, sizeof(client_ip) - 1);
    client_ip[sizeof(client_ip) - 1] = '\0';
    free(args);

    int    backend_fd      = -1;
    int    backend_idx     = -1;
    int    backend_reserved = 0;
    int    success          = 0;
    uint64_t bytes_forwarded = 0;
    uint64_t start_ms       = monotonic_ms();

    metrics_record_request_start();

    /* ------------------------------------------------------------------ */
    /* Step 1: read client request headers                                 */
    /* ------------------------------------------------------------------ */
    char hdr_buf[16384];
    ssize_t hlen = read_with_timeout(client_fd, hdr_buf, sizeof(hdr_buf),
                                     CLIENT_READ_TIMEOUT_MS);
    if (hlen <= 0) {
        /* Timeout or disconnect before headers arrived. */
        goto cleanup;
    }

    /* ------------------------------------------------------------------ */
    /* Step 2: parse request                                               */
    /* ------------------------------------------------------------------ */
    {
        http_request_t req;
        memset(&req, 0, sizeof(req));
        parse_result_t pr = parse_request(hdr_buf, (size_t)hlen, &req);

        if (pr != PARSE_OK) {
            int status;
            const char *status_text;
            const char *err_body;
            switch (pr) {
                case PARSE_METHOD_NOT_ALLOWED:
                    status      = 405;
                    status_text = "Method Not Allowed";
                    err_body    =
                        "{\"error\":{\"code\":\"METHOD_NOT_ALLOWED\","
                        "\"message\":\"only GET is supported\"}}";
                    break;
                case PARSE_URI_TOO_LONG:
                    status      = 414;
                    status_text = "URI Too Long";
                    err_body    =
                        "{\"error\":{\"code\":\"URI_TOO_LONG\","
                        "\"message\":\"request URI too long\"}}";
                    break;
                case PARSE_HEADER_TOO_LARGE:
                    status      = 431;
                    status_text = "Request Header Fields Too Large";
                    err_body    =
                        "{\"error\":{\"code\":\"HEADER_TOO_LARGE\","
                        "\"message\":\"request headers exceed 16 KiB\"}}";
                    break;
                default:
                    status      = 400;
                    status_text = "Bad Request";
                    err_body    =
                        "{\"error\":{\"code\":\"BAD_REQUEST\","
                        "\"message\":\"malformed request\"}}";
                    break;
            }
            send_error_response(client_fd, status, err_body);
            (void)status_text;
            goto cleanup;
        }

        /* ----------------------------------------------------------------
         * Step 3: select backend
         * -------------------------------------------------------------- */
        if (select_backend(s_pool, &backend_idx) != 0) {
            send_error_response(client_fd, 503,
                "{\"error\":{\"code\":\"NO_HEALTHY_BACKEND\","
                "\"message\":\"no healthy backend available\"}}");
            goto cleanup;
        }
        backend_reserved = 1;

        /* ----------------------------------------------------------------
         * Step 4: connect to backend (no lock held)
         * -------------------------------------------------------------- */
        pool_lock();
        backend_t *be = pool_get_backend(backend_idx);
        char be_addr[64];
        int  be_port;
        strncpy(be_addr, be->address, sizeof(be_addr) - 1);
        be_addr[sizeof(be_addr) - 1] = '\0';
        be_port = be->port;
        pool_unlock();

        backend_fd = connect_with_timeout(be_addr, be_port,
                                          BACKEND_CONNECT_TIMEOUT_MS);
        if (backend_fd < 0) {
            send_error_response(client_fd, 502,
                "{\"error\":{\"code\":\"BACKEND_CONNECT_FAILED\","
                "\"message\":\"could not connect to backend\"}}");
            goto cleanup;
        }

        /* ----------------------------------------------------------------
         * Step 5: build and send forwarded GET request
         * -------------------------------------------------------------- */
        /* Separate path and query string. */
        char fwd_target[HTTP_MAX_TARGET_LEN + 1];
        strncpy(fwd_target, req.target, sizeof(fwd_target) - 1);
        fwd_target[sizeof(fwd_target) - 1] = '\0';

        /* Build Host header value. */
        char host_val[128];
        snprintf(host_val, sizeof(host_val), "%s:%d", be_addr, be_port);

        /* Build the forwarded request. */
        char fwd_req[32768];
        size_t fwd_pos = 0;
        int n = snprintf(fwd_req + fwd_pos, sizeof(fwd_req) - fwd_pos,
                         "GET %s HTTP/1.0\r\n"
                         "Host: %s\r\n"
                         "Connection: close\r\n"
                         "X-Forwarded-For: %s\r\n",
                         fwd_target, host_val, client_ip);
        if (n > 0) fwd_pos += (size_t)n;

        /* Forward non-hop-by-hop headers from the original request. */
        static const char * const HOP_BY_HOP[] = {
            "connection", "keep-alive", "transfer-encoding",
            "upgrade", "proxy-authorization", "proxy-authenticate",
            "te", "trailer", NULL
        };

        for (int i = 0; i < req.header_count && fwd_pos < sizeof(fwd_req) - 4; i++) {
            /* Skip hop-by-hop headers (case-insensitive). */
            int skip = 0;
            for (int h = 0; HOP_BY_HOP[h] != NULL; h++) {
                const char *a = req.headers[i].name;
                const char *b = HOP_BY_HOP[h];
                int eq = 1;
                while (*a && *b) {
                    char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a+32) : *a;
                    char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b+32) : *b;
                    if (ca != cb) { eq = 0; break; }
                    a++; b++;
                }
                if (eq && *a == '\0' && *b == '\0') { skip = 1; break; }
            }
            /* Also skip Host — we set it ourselves above. */
            if (!skip) {
                const char *a = req.headers[i].name;
                const char *b = "host";
                int eq = 1;
                while (*a && *b) {
                    char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a+32) : *a;
                    char cb = *b;
                    if (ca != cb) { eq = 0; break; }
                    a++; b++;
                }
                if (eq && *a == '\0' && *b == '\0') skip = 1;
            }
            if (skip) continue;

            n = snprintf(fwd_req + fwd_pos, sizeof(fwd_req) - fwd_pos,
                         "%s: %s\r\n",
                         req.headers[i].name, req.headers[i].value);
            if (n > 0) fwd_pos += (size_t)n;
        }

        /* End of headers. */
        if (fwd_pos + 3 < sizeof(fwd_req)) {
            fwd_req[fwd_pos++] = '\r';
            fwd_req[fwd_pos++] = '\n';
            fwd_req[fwd_pos]   = '\0';
        }

        ssize_t wrc = write_all(backend_fd, fwd_req, fwd_pos);
        if (wrc < 0) {
            send_error_response(client_fd, 502,
                "{\"error\":{\"code\":\"BACKEND_WRITE_FAILED\","
                "\"message\":\"failed to send request to backend\"}}");
            goto cleanup;
        }

        /* ----------------------------------------------------------------
         * Step 6: forward backend response to client
         * -------------------------------------------------------------- */
        ssize_t fb = forward_response(client_fd, backend_fd);
        if (fb < 0) {
            /* Headers never received — count as proxy failure. */
            send_error_response(client_fd, 504,
                "{\"error\":{\"code\":\"BACKEND_READ_TIMEOUT\","
                "\"message\":\"backend did not respond in time\"}}");
            goto cleanup;
        }

        bytes_forwarded = (uint64_t)fb;
        success = 1;

        /* Update per-backend counters under lock. */
        pool_lock();
        be = pool_get_backend(backend_idx);
        if (be) {
            be->requests_total++;
            if (success) be->responses_forwarded++;
            else         be->proxy_failures++;
        }
        pool_unlock();
    }

cleanup:
    {
        uint64_t end_ms     = monotonic_ms();
        double   latency_ms = (double)(end_ms - start_ms);
        metrics_record_request(success, bytes_forwarded, latency_ms);

        if (backend_fd >= 0) {
            close(backend_fd);
            backend_fd = -1;
        }
        close(client_fd);

        if (backend_reserved) {
            release_backend(s_pool, backend_idx);
        }

        /* Decrement active worker count. */
        pthread_mutex_lock(&s_count_mutex);
        if (s_active_count > 0) s_active_count--;
        pthread_mutex_unlock(&s_count_mutex);
    }

    return NULL;
}

/* -----------------------------------------------------------------------
 * proxy_start — create listener and enter accept loop (blocks until stopped)
 * ----------------------------------------------------------------------- */
void proxy_start(backend_pool_t *pool, metrics_t *metrics) {
    s_pool        = pool;
    s_metrics_ptr = metrics;
    s_stop        = 0;

    /* Create listening socket. */
    s_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen_fd < 0) {
        log_error("proxy_start: socket(): %s", strerror(errno));
        return;
    }

    int one = 1;
    setsockopt(s_listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

    if (bind(s_listen_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        log_error("proxy_start: bind(8080): %s", strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return;
    }

    if (listen(s_listen_fd, 128) < 0) {
        log_error("proxy_start: listen(): %s", strerror(errno));
        close(s_listen_fd);
        s_listen_fd = -1;
        return;
    }

    log_info_s("proxy_start: listening on 127.0.0.1:8080");

    /* Accept loop. */
    while (!s_stop) {
        struct sockaddr_in client_sa;
        socklen_t client_sa_len = sizeof(client_sa);

        /* Use select() with a 1-second timeout so we check s_stop. */
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(s_listen_fd, &rset);
        struct timeval tv = {1, 0};
        int rc = select(s_listen_fd + 1, &rset, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) continue; /* timeout — loop and re-check s_stop */

        int client_fd = accept(s_listen_fd,
                               (struct sockaddr *)&client_sa, &client_sa_len);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                continue;
            if (s_stop) break;
            log_error("proxy_start: accept(): %s", strerror(errno));
            continue;
        }

        /* Enforce connection limit. */
        pthread_mutex_lock(&s_count_mutex);
        int over_limit = (s_active_count >= MAX_PROXY_CONNECTIONS);
        if (!over_limit) s_active_count++;
        pthread_mutex_unlock(&s_count_mutex);

        if (over_limit) {
            send_error_response(client_fd, 503,
                "{\"error\":{\"code\":\"TOO_MANY_CONNECTIONS\","
                "\"message\":\"proxy connection limit reached\"}}");
            close(client_fd);
            continue;
        }

        /* Get client IP string. */
        char client_ip[INET_ADDRSTRLEN] = "unknown";
        inet_ntop(AF_INET, &client_sa.sin_addr, client_ip, sizeof(client_ip));

        /* Spawn detached worker thread. */
        proxy_worker_args_t *args = malloc(sizeof(*args));
        if (!args) {
            /* Allocation failure: release count and close. */
            pthread_mutex_lock(&s_count_mutex);
            s_active_count--;
            pthread_mutex_unlock(&s_count_mutex);
            close(client_fd);
            continue;
        }
        args->client_fd = client_fd;
        strncpy(args->client_ip, client_ip, sizeof(args->client_ip) - 1);
        args->client_ip[sizeof(args->client_ip) - 1] = '\0';

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        rc = pthread_create(&tid, &attr, proxy_worker_thread, args);
        pthread_attr_destroy(&attr);

        if (rc != 0) {
            log_error("proxy_start: pthread_create: %s", strerror(rc));
            pthread_mutex_lock(&s_count_mutex);
            s_active_count--;
            pthread_mutex_unlock(&s_count_mutex);
            free(args);
            close(client_fd);
        }
    }

    close(s_listen_fd);
    s_listen_fd = -1;
    log_info_s("proxy_start: listener stopped");
}

/* -----------------------------------------------------------------------
 * proxy_stop — signal accept loop to stop
 * ----------------------------------------------------------------------- */
void proxy_stop(void) {
    s_stop = 1;
    /* Closing the fd wakes up any blocked accept()/select(). */
    if (s_listen_fd >= 0) {
        close(s_listen_fd);
        s_listen_fd = -1;
    }
}
