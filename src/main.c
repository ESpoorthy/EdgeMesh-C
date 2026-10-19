/*
 * src/main.c
 *
 * EdgeMesh-C process entry point.
 *
 * Startup order:
 *   1. sigaction(SIGPIPE, SIG_IGN)
 *   2. pool_init()
 *   3. metrics_init()
 *   4. health_worker_start()
 *   5. metrics tick thread (1-second loop)
 *   6. admin listener thread (127.0.0.1:9090)
 *   7. proxy_start() — blocks until SIGINT/SIGTERM
 *
 * Shutdown: SIGINT/SIGTERM sets g_shutdown, proxy_start() returns, then we
 * stop the health worker, metrics tick thread and admin listener.
 */

#define _POSIX_C_SOURCE 200809L

#include "edgemesh/backend.h"
#include "edgemesh/health.h"
#include "edgemesh/http_response.h"
#include "edgemesh/metrics.h"
#include "edgemesh/proxy.h"
#include "edgemesh/admin_handler.h"
#include "edgemesh/json_builder.h"
#include "edgemesh/log.h"
#include "edgemesh/time_utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * Global shutdown flag (set by signal handler).
 * ----------------------------------------------------------------------- */
volatile sig_atomic_t g_shutdown = 0;

static void handle_signal(int sig) {
    (void)sig;
    g_shutdown = 1;
    proxy_stop();
}

/* -----------------------------------------------------------------------
 * Metrics tick thread: calls metrics_tick() every 1 second.
 * ----------------------------------------------------------------------- */
static volatile int s_tick_stop = 0;

static void *metrics_tick_thread(void *arg) {
    (void)arg;
    while (!s_tick_stop && !g_shutdown) {
        sleep(1);
        if (!s_tick_stop && !g_shutdown) {
            metrics_tick();
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Admin listener state.
 * ----------------------------------------------------------------------- */
static int              s_admin_fd       = -1;
static volatile int     s_admin_stop     = 0;
static pthread_mutex_t  s_admin_count_mx = PTHREAD_MUTEX_INITIALIZER;
static int              s_admin_count    = 0;

/* Per-connection arg. */
typedef struct {
    int client_fd;
} admin_conn_arg_t;

static void *admin_conn_thread(void *arg) {
    admin_conn_arg_t *a = (admin_conn_arg_t *)arg;
    int fd = a->client_fd;
    free(a);

    handle_admin_request(fd);
    /* handle_admin_request closes fd. */

    pthread_mutex_lock(&s_admin_count_mx);
    if (s_admin_count > 0) s_admin_count--;
    pthread_mutex_unlock(&s_admin_count_mx);

    return NULL;
}

static void *admin_listener_thread(void *arg) {
    (void)arg;

    s_admin_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_admin_fd < 0) {
        log_error("admin_listener: socket(): %s", strerror(errno));
        return NULL;
    }

    int one = 1;
    setsockopt(s_admin_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(9090);
    inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr);

    if (bind(s_admin_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        log_error("admin_listener: bind(9090): %s", strerror(errno));
        close(s_admin_fd);
        s_admin_fd = -1;
        return NULL;
    }

    if (listen(s_admin_fd, 32) < 0) {
        log_error("admin_listener: listen(): %s", strerror(errno));
        close(s_admin_fd);
        s_admin_fd = -1;
        return NULL;
    }

    log_info_s("admin_listener: listening on 127.0.0.1:9090");

    while (!s_admin_stop && !g_shutdown) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(s_admin_fd, &rset);
        struct timeval tv = {1, 0};
        int rc = select(s_admin_fd + 1, &rset, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) continue;

        struct sockaddr_in csa;
        socklen_t csa_len = sizeof(csa);
        int cfd = accept(s_admin_fd, (struct sockaddr *)&csa, &csa_len);
        if (cfd < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                continue;
            if (s_admin_stop || g_shutdown) break;
            log_error("admin_listener: accept(): %s", strerror(errno));
            continue;
        }

        /* Enforce admin connection limit. */
        pthread_mutex_lock(&s_admin_count_mx);
        int over = (s_admin_count >= MAX_ADMIN_CONNECTIONS);
        if (!over) s_admin_count++;
        pthread_mutex_unlock(&s_admin_count_mx);

        if (over) {
            send_error_response(cfd, 503,
                "{\"error\":{\"code\":\"TOO_MANY_CONNECTIONS\","
                "\"message\":\"admin connection limit reached\"}}");
            close(cfd);
            continue;
        }

        admin_conn_arg_t *arg2 = malloc(sizeof(*arg2));
        if (!arg2) {
            pthread_mutex_lock(&s_admin_count_mx);
            s_admin_count--;
            pthread_mutex_unlock(&s_admin_count_mx);
            close(cfd);
            continue;
        }
        arg2->client_fd = cfd;

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        rc = pthread_create(&tid, &attr, admin_conn_thread, arg2);
        pthread_attr_destroy(&attr);

        if (rc != 0) {
            log_error("admin_listener: pthread_create: %s", strerror(rc));
            pthread_mutex_lock(&s_admin_count_mx);
            s_admin_count--;
            pthread_mutex_unlock(&s_admin_count_mx);
            free(arg2);
            close(cfd);
        }
    }

    if (s_admin_fd >= 0) {
        close(s_admin_fd);
        s_admin_fd = -1;
    }
    log_info_s("admin_listener: stopped");
    return NULL;
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    /* Capture executable path for benchmark results resolution. */
    if (argc > 0) {
        json_builder_set_exe_path(argv[0]);
    }

    /* 1. Ignore SIGPIPE — use MSG_NOSIGNAL on all send() calls. */
    struct sigaction sa_pipe;
    memset(&sa_pipe, 0, sizeof(sa_pipe));
    sa_pipe.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa_pipe, NULL);

    /* 2. Install SIGINT / SIGTERM handler. */
    struct sigaction sa_term;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = handle_signal;
    sigemptyset(&sa_term.sa_mask);
    sigaction(SIGINT,  &sa_term, NULL);
    sigaction(SIGTERM, &sa_term, NULL);

    /* 3. Initialise backend pool. */
    pool_init();

    /* 4. Initialise metrics. */
    metrics_init();

    /* 5. Start health checker. */
    health_worker_start(&g_pool);

    /* 6. Start metrics tick thread. */
    pthread_t tick_tid;
    if (pthread_create(&tick_tid, NULL, metrics_tick_thread, NULL) != 0) {
        log_error_s("main: failed to start metrics tick thread");
        return 1;
    }

    /* 7. Start admin listener thread. */
    pthread_t admin_tid;
    if (pthread_create(&admin_tid, NULL, admin_listener_thread, NULL) != 0) {
        log_error_s("main: failed to start admin listener thread");
        return 1;
    }

    /* 8. Start proxy (blocks until proxy_stop() is called). */
    log_info_s("EdgeMesh-C starting — proxy on :8080, admin on :9090");
    proxy_start(&g_pool, NULL);

    /* ------------------------------------------------------------------ */
    /* Shutdown sequence.                                                   */
    /* ------------------------------------------------------------------ */
    log_info_s("EdgeMesh-C shutting down…");

    /* Stop health worker. */
    health_worker_stop();

    /* Stop metrics tick thread. */
    s_tick_stop = 1;
    pthread_join(tick_tid, NULL);

    /* Stop admin listener. */
    s_admin_stop = 1;
    if (s_admin_fd >= 0) {
        close(s_admin_fd);
        s_admin_fd = -1;
    }
    pthread_join(admin_tid, NULL);

    log_info_s("EdgeMesh-C exited cleanly");
    return 0;
}
