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

static pid_t start(const char *program, const char *a, const char *b, const char *c, const char *d) {
    pid_t pid = fork();
    if (pid == 0) { execl(program, program, a, b, c, d, (char *)NULL); _exit(127); }
    return pid;
}

static int wait_port(unsigned short port) {
    for (int i = 0; i < 100; ++i) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons(port); (void)inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
        int rc = connect(fd, (struct sockaddr *)&a, sizeof(a)); close(fd);
        if (rc == 0) return 0;
        struct timespec t = {0, 20000000L}; nanosleep(&t, NULL);
    }
    return -1;
}

static int request(const char *text, char *out, size_t cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) return -1;
    struct timeval tv = {3, 0}; (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons(18080); (void)inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return -1; }
    size_t left = strlen(text), sent = 0U;
    while (sent < left) { ssize_t n = send(fd, text + sent, left - sent, 0); if (n <= 0) { close(fd); return -1; } sent += (size_t)n; }
    size_t used = 0U;
    while (used + 1U < cap) { ssize_t n = recv(fd, out + used, cap - used - 1U, 0); if (n <= 0) break; used += (size_t)n; }
    out[used] = '\0'; close(fd); return (int)used;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    (void)signal(SIGPIPE, SIG_IGN);
    pid_t backend = start(argv[1], "--port", "19001", "--id", "backend-1");
    if (backend < 0 || wait_port(19001U) != 0) { fprintf(stderr, "backend did not start\n"); return 1; }
    pid_t proxy = start("./edgemesh-proxy", "--listen-port", "18080", "--backend-port", "19001");
    if (proxy < 0 || wait_port(18080U) != 0) { kill(backend, SIGTERM); waitpid(backend, NULL, 0); fprintf(stderr, "proxy did not start\n"); return 1; }
    int failed = 0; char response[32768];
    if (request("GET /demo?q=1 HTTP/1.1\r\nHost: localhost\r\n\r\n", response, sizeof(response)) < 0 || strstr(response, "200 OK") == NULL || strstr(response, "X-Backend-ID: backend-1") == NULL || strstr(response, "Hello from backend-1") == NULL) {
        fprintf(stderr, "successful proxied GET failed\n%s\n", response); ++failed;
    }
    if (request("GET / HTTP/1.1\r\nMissingColon\r\n\r\n", response, sizeof(response)) < 0 || strstr(response, "400 Bad Request") == NULL) {
        fprintf(stderr, "malformed request was not rejected\n%s\n", response); ++failed;
    }
    kill(backend, SIGTERM); waitpid(backend, NULL, 0);
    if (request("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", response, sizeof(response)) < 0 || strstr(response, "502 Bad Gateway") == NULL) {
        fprintf(stderr, "unavailable backend did not return 502\n%s\n", response); ++failed;
    }
    kill(proxy, SIGTERM); waitpid(proxy, NULL, 0);
    return failed == 0 ? 0 : 1;
}
