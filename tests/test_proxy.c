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

static int verify_connection_capacity(void) {
    int idle[128];
    size_t opened = 0U;
    for (size_t i = 0U; i < sizeof(idle) / sizeof(idle[0]); ++i) idle[i] = -1;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(18080U);
    (void)inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    for (; opened < sizeof(idle) / sizeof(idle[0]); ++opened) {
        idle[opened] = socket(AF_INET, SOCK_STREAM, 0);
        if (idle[opened] < 0 || connect(idle[opened], (struct sockaddr *)&address, sizeof(address)) != 0) {
            if (idle[opened] >= 0) close(idle[opened]);
            for (size_t j = 0U; j < opened; ++j) close(idle[j]);
            return -1;
        }
    }
    /* Give the accept loop time to admit all idle clients, well inside the 2s header deadline. */
    pause_ms(100L);

    int overflow = socket(AF_INET, SOCK_STREAM, 0);
    if (overflow < 0) {
        for (size_t j = 0U; j < opened; ++j) close(idle[j]);
        return -1;
    }
    struct timeval timeout = { 1, 0 };
    (void)setsockopt(overflow, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int connected = connect(overflow, (struct sockaddr *)&address, sizeof(address));
    char byte = '\0';
    ssize_t received = connected == 0 ? recv(overflow, &byte, 1U, 0) : -1;
    close(overflow);
    for (size_t j = 0U; j < opened; ++j) close(idle[j]);
    pause_ms(150L);
    return connected == 0 && received == 0 ? 0 : -1;
}

static size_t count_substring(const char *text, const char *needle) {
    size_t count = 0U;
    size_t needle_length = strlen(needle);
    if (needle_length == 0U) return 0U;
    const char *p = text;
    while ((p = strstr(p, needle)) != NULL) {
        ++count;
        p += needle_length;
    }
    return count;
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

    if (strcmp(argv[3], "round_robin") == 0 && verify_connection_capacity() != 0) {
        fprintf(stderr, "proxy connection cap did not close excess client sockets without a response\n");
        failed = 1;
        goto cleanup;
    }

    unsigned counts[EM_BACKEND_COUNT] = { 0U, 0U, 0U };
    for (unsigned i = 0U; i < 6U; ++i) {
        static const char request_text[] = "GET /batch HTTP/1.1\r\nHost: localhost\r\n\r\n";
        if (request(request_text, response, sizeof(response)) < 0 || strstr(response, "200 OK") == NULL) {
            fprintf(stderr, "proxied request failed\n%s\n", response);
            failed = 1;
            goto cleanup;
        }
        if (count_substring(response, "Connection:") != 1U ||
            strstr(response, "Transfer-Encoding:") != NULL ||
            strstr(response, "Proxy-Connection:") != NULL) {
            fprintf(stderr, "response hop-by-hop headers were not filtered\n%s\n", response);
            ++failed;
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
    /* Before the three-failure probe threshold, a refused upstream connect must retry
       another healthy backend without leaking a client-facing 502. */
    bool retry_survived_failure = false;
    for (unsigned i = 0U; i < 3U; ++i) {
        static const char request_text[] = "GET /immediate-failover HTTP/1.1\r\nHost: localhost\r\n\r\n";
        if (request(request_text, response, sizeof(response)) < 0 ||
            strstr(response, "200 OK") == NULL ||
            strstr(response, "X-Backend-ID: backend-2") != NULL) {
            fprintf(stderr, "immediate retry after backend failure failed\n%s\n", response);
            ++failed;
            break;
        }
        if (strstr(response, "X-Backend-ID: backend-3") != NULL) retry_survived_failure = true;
    }
    if (!retry_survived_failure) {
        fprintf(stderr, "integration traffic did not exercise retry from the stopped backend\n");
        ++failed;
    }

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
