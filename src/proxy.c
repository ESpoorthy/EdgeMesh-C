#define _POSIX_C_SOURCE 200809L
#include "edgemesh/proxy.h"
#include "edgemesh/http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define IO_TIMEOUT_MS 2000
#define REQUEST_DEADLINE_MS 9000

static int64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000L);
}

static int wait_fd(int fd, short events, int64_t deadline) {
    for (;;) {
        int64_t remaining = deadline - now_ms();
        if (remaining <= 0) return 0;
        struct pollfd pfd = { .fd = fd, .events = events, .revents = 0 };
        int rc = poll(&pfd, 1, remaining > INT32_MAX ? INT32_MAX : (int)remaining);
        if (rc > 0) return (pfd.revents & (events | POLLERR | POLLHUP)) != 0 ? 1 : -1;
        if (rc == 0) return 0;
        if (errno != EINTR) return -1;
    }
}

static ssize_t recv_until(int fd, void *buf, size_t cap, int64_t deadline) {
    for (;;) {
        int ready = wait_fd(fd, POLLIN, deadline);
        if (ready <= 0) return ready == 0 ? -2 : -1;
        ssize_t n = recv(fd, buf, cap, 0);
        if (n >= 0) return n;
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
    }
}

static int send_all(int fd, const void *buffer, size_t length, int64_t deadline) {
    const unsigned char *p = buffer;
    size_t sent = 0U;
    while (sent < length) {
        if (wait_fd(fd, POLLOUT, deadline) <= 0) return -1;
#ifdef MSG_NOSIGNAL
        ssize_t n = send(fd, p + sent, length - sent, MSG_NOSIGNAL);
#else
        ssize_t n = send(fd, p + sent, length - sent, 0);
#endif
        if (n > 0) sent += (size_t)n;
        else if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        else if (n == 0) return -1;
    }
    return 0;
}

static void generated_error(int fd, int status, const char *reason, const char *body, int64_t deadline) {
    char response[512];
    size_t body_len = strlen(body);
    int n = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
        status, reason, body_len, body);
    if (n > 0 && (size_t)n < sizeof(response)) (void)send_all(fd, response, (size_t)n, deadline);
}

static int connect_backend(unsigned short port, int64_t deadline) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); return -1; }
#ifdef SO_NOSIGPIPE
    int one = 1; (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_port = htons(port);
    (void)inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    if (rc < 0) {
        if (wait_fd(fd, POLLOUT, deadline) <= 0) { close(fd); return -1; }
        int error = 0; socklen_t len = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) != 0 || error != 0) { close(fd); return -1; }
    }
    return fd;
}

static bool header_done(const unsigned char *buf, size_t n) {
    if (n < 4U) return false;
    for (size_t i = 3U; i < n; ++i) if (buf[i-3U]=='\r' && buf[i-2U]=='\n' && buf[i-1U]=='\r' && buf[i]=='\n') return true;
    return false;
}

static bool valid_upstream_headers(const unsigned char *data, size_t header_end, long long *content_length) {
    if (header_end < 12U || header_end > EM_RESPONSE_HEADER_MAX) return false;
    const unsigned char *line_end = NULL;
    for (size_t i = 1U; i < header_end; ++i) if (data[i-1U] == '\r' && data[i] == '\n') { line_end = data + i - 1U; break; }
    if (line_end == NULL) return false;
    size_t status_len = (size_t)(line_end - data);
    if (status_len < 12U || (memcmp(data, "HTTP/1.0 ", 9U) != 0 && memcmp(data, "HTTP/1.1 ", 9U) != 0) || data[9] < '2' || data[9] > '5' || data[10] < '0' || data[10] > '9' || data[11] < '0' || data[11] > '9' || (status_len > 12U && data[12] != ' ')) return false;
    unsigned content_lengths = 0U;
    bool transfer_encoding = false;
    *content_length = -1LL;
    const unsigned char *p = line_end + 2;
    const unsigned char *limit = data + header_end - 2U;
    while (p < limit) {
        const unsigned char *eol = NULL;
        for (const unsigned char *q = p; q + 1 < data + header_end; ++q) if (q[0] == '\r' && q[1] == '\n') { eol = q; break; }
        if (eol == NULL) return false;
        size_t line_len = (size_t)(eol - p);
        if (line_len == 0U) break;
        if (*p == ' ' || *p == '\t') return false;
        const unsigned char *colon = memchr(p, ':', line_len);
        if (colon == NULL || colon == p) return false;
        size_t name_len = (size_t)(colon - p);
        for (size_t i = 0; i < name_len; ++i) {
            unsigned char c = p[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", (int)c) != NULL)) return false;
        }
        const unsigned char *v = colon + 1, *ve = eol;
        while (v < ve && (*v == ' ' || *v == '\t')) ++v;
        while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) --ve;
        for (const unsigned char *q = v; q < ve; ++q) if ((*q < 0x20U && *q != '\t') || *q == 0x7fU) return false;
        if (name_len == 17U && strncasecmp((const char *)p, "Transfer-Encoding", 17U) == 0) transfer_encoding = true;
        if (name_len == 14U && strncasecmp((const char *)p, "Content-Length", 14U) == 0) {
            if (++content_lengths > 1U || v == ve) return false;
            unsigned long long value = 0ULL;
            for (const unsigned char *q = v; q < ve; ++q) {
                if (*q < '0' || *q > '9') return false;
                unsigned digit = (unsigned)(*q - '0');
                if (value > (unsigned long long)EM_RESPONSE_BODY_MAX / 10ULL || value * 10ULL + digit > (unsigned long long)EM_RESPONSE_BODY_MAX) return false;
                value = value * 10ULL + digit;
            }
            *content_length = (long long)value;
        }
        p = eol + 2;
    }
    return !transfer_encoding;
}

static int read_request(int fd, unsigned char *buf, size_t *length, int64_t deadline) {
    *length = 0U;
    while (*length < EM_REQUEST_HEADER_MAX) {
        ssize_t n = recv_until(fd, buf + *length, EM_REQUEST_HEADER_MAX - *length, deadline);
        if (n == -2) return 408;
        if (n <= 0) return 400;
        *length += (size_t)n;
        if (header_done(buf, *length)) return 0;
    }
    size_t first_line = 0U;
    while (first_line + 1U < *length && !(buf[first_line] == '\r' && buf[first_line + 1U] == '\n')) ++first_line;
    return first_line > EM_REQUEST_LINE_MAX ? 414 : 431;
}

/* Forward only parsed GET requests. The destination is always 127.0.0.1:backend_port. */
static void handle_client(int client, unsigned short backend_port, int64_t accepted_at) {
    unsigned char request[EM_REQUEST_HEADER_MAX];
    size_t request_len = 0U;
    int status = read_request(client, request, &request_len, accepted_at + IO_TIMEOUT_MS);
    if (status != 0) {
        const char *reason = status == 408 ? "Request Timeout" : status == 431 ? "Request Header Fields Too Large" : "Bad Request";
        const char *body = status == 408 ? "request timeout\n" : status == 431 ? "headers too large\n" : "bad request\n";
        generated_error(client, status, reason, body, accepted_at + REQUEST_DEADLINE_MS);
        return;
    }
    em_request parsed;
    status = em_parse_request(request, request_len, &parsed);
    if (status != 0) {
        const char *reason = "Bad Request", *body = "bad request\n";
        if (status == 405) { reason = "Method Not Allowed"; body = "method not allowed\n"; }
        else if (status == 414) { reason = "URI Too Long"; body = "request line too long\n"; }
        else if (status == 413) { reason = "Content Too Large"; body = "request body not supported\n"; }
        else if (status == 431) { reason = "Request Header Fields Too Large"; body = "headers too large\n"; }
        else if (status == 505) { reason = "HTTP Version Not Supported"; body = "HTTP version not supported\n"; }
        if (status == 405) {
            char out[512]; size_t body_len = strlen(body);
            int n = snprintf(out, sizeof(out), "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", body_len, body);
            if (n > 0 && (size_t)n < sizeof(out)) (void)send_all(client, out, (size_t)n, accepted_at + REQUEST_DEADLINE_MS);
        } else generated_error(client, status, reason, body, accepted_at + REQUEST_DEADLINE_MS);
        return;
    }

    int64_t attempt_deadline = now_ms() + IO_TIMEOUT_MS;
    if (attempt_deadline > accepted_at + REQUEST_DEADLINE_MS - 1000) attempt_deadline = accepted_at + REQUEST_DEADLINE_MS - 1000;
    int upstream = connect_backend(backend_port, attempt_deadline);
    if (upstream < 0) { generated_error(client, 502, "Bad Gateway", "upstream unavailable\n", accepted_at + REQUEST_DEADLINE_MS); return; }
    char forwarded[EM_REQUEST_HEADER_MAX + 256U];
    int f = snprintf(forwarded, sizeof(forwarded), "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nConnection: close\r\n\r\n", parsed.target, (unsigned)backend_port);
    if (f < 0 || (size_t)f >= sizeof(forwarded) || send_all(upstream, forwarded, (size_t)f, attempt_deadline) != 0) {
        close(upstream); generated_error(client, 502, "Bad Gateway", "upstream failure\n", accepted_at + REQUEST_DEADLINE_MS); return;
    }

    size_t cap = EM_RESPONSE_HEADER_MAX + EM_RESPONSE_BODY_MAX;
    unsigned char *response = malloc(cap);
    if (response == NULL) { close(upstream); generated_error(client, 502, "Bad Gateway", "upstream failure\n", accepted_at + REQUEST_DEADLINE_MS); return; }
    size_t used = 0U; bool done = false; size_t header_end = 0U; long long content_length = -1LL;
    while (used < cap && !done) {
        ssize_t n = recv_until(upstream, response + used, cap - used, attempt_deadline);
        if (n <= 0) {
            if (n == 0) done = true;
            else break;
        } else {
            used += (size_t)n;
            if (header_end == 0U && header_done(response, used)) {
                for (size_t i = 3U; i < used; ++i) if (response[i-3U]=='\r' && response[i-2U]=='\n' && response[i-1U]=='\r' && response[i]=='\n') { header_end = i + 1U; break; }
                if (header_end > EM_RESPONSE_HEADER_MAX) break;
                const unsigned char *cl = NULL;
                for (size_t i = 0; i + 15U < header_end; ++i) if ((i == 0U || (response[i-1U]=='\n')) && strncasecmp((char *)response + i, "Content-Length:", 15U) == 0) { cl = response + i + 15U; break; }
                if (cl != NULL) {
                    while (*cl == ' ' || *cl == '\t') ++cl;
                    char *endptr = NULL; content_length = strtoll((char *)cl, &endptr, 10);
                    if (endptr == (char *)cl || content_length < 0LL || (unsigned long long)content_length > EM_RESPONSE_BODY_MAX) break;
                    if ((size_t)content_length <= used - header_end) done = true;
                }
            }
            if (header_end > 0U && content_length >= 0LL && used >= header_end + (size_t)content_length) done = true;
        }
    }
    close(upstream);
    bool valid = header_end > 0U && used >= header_end && valid_upstream_headers(response, header_end, &content_length) && (content_length < 0LL ? done : used >= header_end + (size_t)content_length) && used <= cap;
    if (!valid) generated_error(client, 502, "Bad Gateway", "invalid upstream response\n", accepted_at + REQUEST_DEADLINE_MS);
    else (void)send_all(client, response, content_length >= 0LL ? header_end + (size_t)content_length : used, accepted_at + REQUEST_DEADLINE_MS);
    free(response);
}

int em_proxy_run(unsigned short listen_port, unsigned short backend_port) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return -1;
    int one = 1; (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_port = htons(listen_port);
    (void)inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(listener, 64) != 0) { close(listener); return -1; }
    for (;;) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) { if (errno == EINTR) continue; close(listener); return -1; }
#ifdef SO_NOSIGPIPE
        (void)setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        int flags = fcntl(client, F_GETFL, 0);
        if (flags >= 0) (void)fcntl(client, F_SETFL, flags | O_NONBLOCK);
        handle_client(client, backend_port, now_ms());
        close(client);
    }
}
