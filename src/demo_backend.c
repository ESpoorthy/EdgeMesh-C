#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int parse_port(const char *s, unsigned short *out) {
    char *end = NULL; errno = 0; unsigned long value = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value == 0UL || value > 65535UL) return -1;
    *out = (unsigned short)value; return 0;
}

static void serve(int fd, const char *id) {
    char request[16384]; size_t used = 0U;
    while (used < sizeof(request) - 1U) {
        ssize_t n = recv(fd, request + used, sizeof(request) - used - 1U, 0);
        if (n <= 0) return;
        used += (size_t)n; request[used] = '\0';
        if (strstr(request, "\r\n\r\n") != NULL) break;
    }
    if (used == 0U) return;
    bool health = strncmp(request, "GET /health ", 12U) == 0;
    const char *body = health ? "ok\n" : NULL;
    char default_body[128];
    if (!health) { (void)snprintf(default_body, sizeof(default_body), "Hello from %s\n", id); body = default_body; }
    char response[512]; size_t body_len = strlen(body);
    int n = snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nX-Backend-ID: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
        id, body_len, body);
    if (n > 0 && (size_t)n < sizeof(response)) {
        size_t sent = 0U;
        while (sent < (size_t)n) { ssize_t w = send(fd, response + sent, (size_t)n - sent, 0); if (w <= 0) break; sent += (size_t)w; }
    }
}

int main(int argc, char **argv) {
    unsigned short port = 9001U; const char *id = "backend-1";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) { if (parse_port(argv[++i], &port) != 0) return 2; }
        else if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) { id = argv[++i]; }
        else { fprintf(stderr, "usage: %s [--port PORT] [--id backend-1]\n", argv[0]); return 2; }
    }
    if (strcmp(id, "backend-1") != 0 && strcmp(id, "backend-2") != 0 && strcmp(id, "backend-3") != 0) { fprintf(stderr, "id must be backend-1, backend-2 or backend-3\n"); return 2; }
    (void)signal(SIGPIPE, SIG_IGN);
    int listener = socket(AF_INET, SOCK_STREAM, 0); if (listener < 0) return 1;
    int one = 1; (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_port = htons(port); (void)inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 16) != 0) { close(listener); return 1; }
    for (;;) { int client = accept(listener, NULL, NULL); if (client < 0) { if (errno == EINTR) continue; close(listener); return 1; } serve(client, id); close(client); }
}
