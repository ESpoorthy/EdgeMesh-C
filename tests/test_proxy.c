#define _POSIX_C_SOURCE 200809L
#include "edgemesh/proxy.h"

#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void pause_ms(long milliseconds) {
    struct timespec delay = {
        .tv_sec = (time_t)(milliseconds / 1000L),
        .tv_nsec = (long)((milliseconds % 1000L) * 1000000L)
    };
    while (nanosleep(&delay, &delay) != 0) {
        /* Retry interrupted sleeps. */
    }
}

static pid_t start_backend(const char *program, unsigned short port, const char *id) {
    pid_t pid = fork();
    if (pid == 0) {
        char port_text[8];
        (void)snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
        execl(program, program, "--port", port_text, "--id", id, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static pid_t start_proxy(const char *program, const char *policy) {
    pid_t pid = fork();
    if (pid == 0) {
        execl(program, program, "--listen-port", "18080", "--backend-port", "19001",
              "--policy", policy, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static int wait_port(unsigned short port) {
    for (int i = 0; i < 150; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        (void)inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        int rc = connect(fd, (struct sockaddr *)&address, sizeof(address));
        close(fd);
        if (rc == 0) return 0;
        pause_ms(20L);
    }
    return -1;
}

static int request(const char *text, char *out, size_t capacity) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval timeout = { 3, 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(18080U);
    (void)inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
    }
    size_t length = strlen(text);
    size_t sent = 0U;
    while (sent < length) {
        ssize_t n = send(fd, text + sent, length - sent, 0);
        if (n <= 0) { close(fd); return -1; }
        sent += (size_t)n;
    }
    size_t used = 0U;
    while (used + 1U < capacity) {
        ssize_t n = recv(fd, out + used, capacity - used - 1U, 0);
        if (n <= 0) break;
        used += (size_t)n;
    }
    out[used] = '\0';
    close(fd);
    return (int)used;
}

static void stop_process(pid_t *pid) {
    if (*pid > 0) {
        (void)kill(*pid, SIGTERM);
        (void)waitpid(*pid, NULL, 0);
        *pid = -1;
    }
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    (void)signal(SIGPIPE, SIG_IGN);
    pid_t backends[EM_BACKEND_COUNT] = { -1, -1, -1 };
    pid_t proxy = -1;
    int failed = 0;
    char response[32768] = { 0 };

    for (size_t i = 0U; i < EM_BACKEND_COUNT; ++i) {
        char id[16];
        (void)snprintf(id, sizeof(id), "backend-%u", (unsigned)(i + 1U));
        backends[i] = start_backend(argv[1], (unsigned short)(19001U + (unsigned)i), id);
        if (backends[i] < 0 || wait_port((unsigned short)(19001U + (unsigned)i)) != 0) {
            fprintf(stderr, "backend %zu failed to start\n", i + 1U);
            failed = 1;
            goto cleanup;
        }
    }

    proxy = start_proxy(argv[2], argv[3]);
    if (proxy < 0 || wait_port(18080U) != 0) {
        fprintf(stderr, "proxy failed to start\n");
        failed = 1;
        goto cleanup;
    }
    /* Two complete successful probes are required before routing to each backend. */
    pause_ms(2300L);

    unsigned counts[EM_BACKEND_COUNT] = { 0U, 0U, 0U };
    for (unsigned i = 0U; i < 6U; ++i) {
        static const char request_text[] = "GET /batch HTTP/1.1\r\nHost: localhost\r\n\r\n";
        if (request(request_text, response, sizeof(response)) < 0 || strstr(response, "200 OK") == NULL) {
            fprintf(stderr, "proxied request failed\n%s\n", response);
            failed = 1;
            goto cleanup;
        }
        for (size_t j = 0U; j < EM_BACKEND_COUNT; ++j) {
            char id_header[64];
            (void)snprintf(id_header, sizeof(id_header), "X-Backend-ID: backend-%u", (unsigned)(j + 1U));
            if (strstr(response, id_header) != NULL) ++counts[j];
        }
    }
    if (counts[0] != 2U || counts[1] != 2U || counts[2] != 2U) {
        fprintf(stderr, "round-robin distribution mismatch: %u,%u,%u\n", counts[0], counts[1], counts[2]);
        ++failed;
    }

    if (request("GET / HTTP/1.1\r\nMissingColon\r\n\r\n", response, sizeof(response)) < 0 ||
        strstr(response, "400 Bad Request") == NULL) {
        fprintf(stderr, "malformed request was not rejected\n%s\n", response);
        ++failed;
    }

    stop_process(&backends[1]);
    pause_ms(4300L);
    for (unsigned i = 0U; i < 6U; ++i) {
        static const char request_text[] = "GET /failover HTTP/1.1\r\nHost: localhost\r\n\r\n";
        if (request(request_text, response, sizeof(response)) < 0 ||
            strstr(response, "200 OK") == NULL ||
            strstr(response, "X-Backend-ID: backend-2") != NULL) {
            fprintf(stderr, "unhealthy backend received new work or failover failed\n%s\n", response);
            ++failed;
            break;
        }
    }

    backends[1] = start_backend(argv[1], 19002U, "backend-2");
    if (backends[1] < 0 || wait_port(19002U) != 0) {
        fprintf(stderr, "backend-2 failed to restart\n");
        ++failed;
        goto cleanup;
    }
    pause_ms(3300L);
    bool recovered_seen = false;
    for (unsigned i = 0U; i < 6U; ++i) {
        static const char request_text[] = "GET /recovery HTTP/1.1\r\nHost: localhost\r\n\r\n";
        if (request(request_text, response, sizeof(response)) < 0 || strstr(response, "200 OK") == NULL) {
            fprintf(stderr, "request failed during recovery check\n%s\n", response);
            ++failed;
            break;
        }
        if (strstr(response, "X-Backend-ID: backend-2") != NULL) recovered_seen = true;
    }
    if (!recovered_seen) {
        fprintf(stderr, "recovered backend was not selected after successful probes\n");
        ++failed;
    }

    stop_process(&backends[0]);
    stop_process(&backends[1]);
    stop_process(&backends[2]);
    pause_ms(4300L);
    if (request("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", response, sizeof(response)) < 0 ||
        strstr(response, "503 Service Unavailable") == NULL) {
        fprintf(stderr, "all-unhealthy pool did not return 503\n%s\n", response);
        ++failed;
    }

cleanup:
    stop_process(&proxy);
    for (size_t i = 0U; i < EM_BACKEND_COUNT; ++i) stop_process(&backends[i]);
    return failed == 0 ? 0 : 1;
}
