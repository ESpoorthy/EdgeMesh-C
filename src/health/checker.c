#include "edgemesh/health.h"
#include "edgemesh/log.h"
#include "edgemesh/time_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

/* Probe timeouts (architecture.md §5a). */
#define PROBE_CONNECT_TIMEOUT_S 3
#define PROBE_READ_TIMEOUT_S    3

static pthread_t         s_thread;
static volatile int      s_stop   = 0;
static backend_pool_t   *s_pool   = NULL;

/* --------------------------------------------------------------------- */
/* Internal: connect with a timeout using non-blocking socket + select()  */
/* --------------------------------------------------------------------- */
static int connect_with_timeout(const char *addr, int port, int timeout_s) {
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
        /* Immediately connected (unusual but valid). */
        /* Restore blocking mode. */
        fcntl(fd, F_SETFL, flags);
        return fd;
    }
    if (errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(fd, &wset);
    struct timeval tv = { .tv_sec = timeout_s, .tv_usec = 0 };

    rc = select(fd + 1, NULL, &wset, NULL, &tv);
    if (rc <= 0) {
        close(fd);
        return -1;
    }

    /* Check for connect error via SO_ERROR. */
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err != 0) {
        close(fd);
        return -1;
    }

    /* Restore blocking mode. */
    fcntl(fd, F_SETFL, flags);
    return fd;
}

/* --------------------------------------------------------------------- */
/* probe_backend — returns 1 on healthy response, 0 on failure            */
/* Also fills out response_ms.                                            */
/* --------------------------------------------------------------------- */
static int probe_backend(const char *addr, int port, uint64_t *out_ms) {
    uint64_t t0 = monotonic_ms();

    int fd = connect_with_timeout(addr, port, PROBE_CONNECT_TIMEOUT_S);
    if (fd < 0) {
        *out_ms = 0;
        return 0;
    }

    /* Send minimal HTTP/1.0 request (no Host header to keep it simple). */
    const char req[] = "GET / HTTP/1.0\r\n\r\n";
    ssize_t n = send(fd, req, sizeof(req) - 1, MSG_NOSIGNAL);
    if (n <= 0) {
        close(fd);
        *out_ms = 0;
        return 0;
    }

    /* Read response with timeout. */
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    struct timeval tv = { .tv_sec = PROBE_READ_TIMEOUT_S, .tv_usec = 0 };
    int rc = select(fd + 1, &rset, NULL, NULL, &tv);
    if (rc <= 0) {
        close(fd);
        *out_ms = 0;
        return 0;
    }

    char buf[256];
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    *out_ms = monotonic_ms() - t0;

    if (n <= 0) return 0;
    buf[n] = '\0';

    /* Minimal check: response starts with "HTTP/1." */
    return (strncmp(buf, "HTTP/1.", 7) == 0) ? 1 : 0;
}

/* --------------------------------------------------------------------- */
/* Health-check worker thread                                              */
/* --------------------------------------------------------------------- */
static void *health_worker(void *arg) {
    (void)arg;

    while (!s_stop) {
        for (int i = 0; i < BACKEND_POOL_SIZE && !s_stop; i++) {
            /* Read address/port under lock before doing I/O. */
            char   addr[64];
            int    port;
            pthread_mutex_lock(&s_pool->mutex);
            strncpy(addr, s_pool->backends[i].address, sizeof(addr) - 1);
            addr[sizeof(addr) - 1] = '\0';
            port = s_pool->backends[i].port;
            pthread_mutex_unlock(&s_pool->mutex);

            /* Perform network probe WITHOUT holding the lock. */
            uint64_t resp_ms = 0;
            int healthy = probe_backend(addr, port, &resp_ms);

            /* Record result under lock. */
            char ts[24];
            rfc3339_now(ts, sizeof(ts));

            pthread_mutex_lock(&s_pool->mutex);
            backend_t *b = &s_pool->backends[i];
            b->last_response_ms = resp_ms;
            strncpy(b->last_check_at, ts, sizeof(b->last_check_at) - 1);

            backend_state_t old_state = b->state;

            if (healthy) {
                b->consec_successes++;
                b->consec_failures = 0;
                if (b->consec_successes >= HEALTH_PASS_THRESHOLD) {
                    b->state = BACKEND_HEALTHY;
                }
            } else {
                b->consec_failures++;
                b->consec_successes = 0;
                if (b->consec_failures >= HEALTH_FAIL_THRESHOLD) {
                    b->state = BACKEND_UNHEALTHY;
                }
            }

            backend_state_t new_state = b->state;
            pthread_mutex_unlock(&s_pool->mutex);

            if (old_state != new_state) {
                const char *state_str =
                    (new_state == BACKEND_HEALTHY) ? "HEALTHY" : "UNHEALTHY";
                log_info("health: backend[%d] %s:%d → %s",
                         i, addr, port, state_str);
            }
        }

        /* Sleep HEALTH_PROBE_INTERVAL_S seconds, checking s_stop every 100 ms. */
        for (int ms = 0; ms < HEALTH_PROBE_INTERVAL_S * 1000 && !s_stop;
             ms += 100) {
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 100 * 1000000L };
            nanosleep(&ts, NULL);
        }
    }

    return NULL;
}

/* --------------------------------------------------------------------- */
/* Public API                                                              */
/* --------------------------------------------------------------------- */
void health_worker_start(backend_pool_t *pool) {
    s_pool = pool;
    s_stop = 0;
    if (pthread_create(&s_thread, NULL, health_worker, NULL) != 0) {
        log_error_s("health_worker_start: pthread_create failed");
    }
}

void health_worker_stop(void) {
    s_stop = 1;
    pthread_join(s_thread, NULL);
}
