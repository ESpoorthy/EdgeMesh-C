/*
 * src/demo_backend/demo_backend.c
 *
 * Minimal HTTP/1.1 demo server.
 * Usage: demo_backend <port>
 *   port must be 1–65535.
 *
 * For each accepted connection:
 *   - reads (and discards) the request
 *   - responds with "Backend at port <port>\n"
 *   - closes the connection
 *
 * Sequential (single-threaded) accept loop — sufficient for demo purposes.
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: demo_backend <port>\n");
        return 1;
    }

    /* Validate port argument. */
    char *endp;
    long port = strtol(argv[1], &endp, 10);
    if (*endp != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "demo_backend: invalid port '%s' (must be 1-65535)\n",
                argv[1]);
        return 1;
    }

    /* Create and bind listening socket. */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("demo_backend: socket");
        return 1;
    }

    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr) != 1) {
        fprintf(stderr, "demo_backend: inet_pton failed\n");
        close(lfd);
        return 1;
    }

    if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("demo_backend: bind");
        close(lfd);
        return 1;
    }

    if (listen(lfd, 16) < 0) {
        perror("demo_backend: listen");
        close(lfd);
        return 1;
    }

    printf("demo_backend listening on 127.0.0.1:%ld\n", port);
    fflush(stdout);

    /* Build the response body and full HTTP response once. */
    char body[128];
    int body_len = snprintf(body, sizeof(body),
                            "Backend at port %ld\n", port);
    if (body_len < 0) body_len = 0;

    char response[512];
    int resp_len = snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "Content-Length: %d\r\n"
        "\r\n"
        "%s",
        body_len, body);
    if (resp_len < 0) resp_len = 0;

    /* Sequential accept loop. */
    for (;;) {
        struct sockaddr_in csa;
        socklen_t csa_len = sizeof(csa);
        int cfd = accept(lfd, (struct sockaddr *)&csa, &csa_len);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("demo_backend: accept");
            break;
        }

        /* Read and discard the request (drain until \r\n\r\n or timeout). */
        char req_buf[4096];
        size_t req_total = 0;
        int found_end = 0;
        while (!found_end && req_total < sizeof(req_buf) - 1) {
            fd_set rset;
            FD_ZERO(&rset);
            FD_SET(cfd, &rset);
            struct timeval tv = {5, 0};
            int rc = select(cfd + 1, &rset, NULL, NULL, &tv);
            if (rc <= 0) break;

            ssize_t n = read(cfd, req_buf + req_total,
                             sizeof(req_buf) - 1 - req_total);
            if (n <= 0) break;
            req_total += (size_t)n;
            req_buf[req_total] = '\0';

            /* Check for end-of-headers sentinel. */
            if (strstr(req_buf, "\r\n\r\n") != NULL) {
                found_end = 1;
            }
        }

        /* Send the response. */
        size_t sent = 0;
        while (sent < (size_t)resp_len) {
            ssize_t n = send(cfd, response + sent,
                             (size_t)resp_len - sent, MSG_NOSIGNAL);
            if (n <= 0) break;
            sent += (size_t)n;
        }

        close(cfd);
    }

    close(lfd);
    return 0;
}
