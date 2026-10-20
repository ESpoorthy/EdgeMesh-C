#define _POSIX_C_SOURCE 200809L
#include "edgemesh/proxy.h"
#include "edgemesh/backend_pool.h"
#include "edgemesh/http.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
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
#define HEALTH_INTERVAL_MS 1000
#define HEALTH_TIMEOUT_MS 250
#define MAX_PROXY_CONNECTIONS 128U
#define LISTEN_BACKLOG 64
#define HEALTH_RESPONSE_MAX 2048U

typedef struct {
    int client_fd;
    int64_t accepted_at_ms;
} client_work;

static em_backend_pool g_pool;
static pthread_mutex_t g_capacity_mutex = PTHREAD_MUTEX_INITIALIZER;
static size_t g_active_clients = 0U;

static int64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000L);
}

static void sleep_until_ms(int64_t deadline) {
    for (;;) {
        int64_t remaining = deadline - now_ms();
        if (remaining <= 0) return;
        struct timespec delay = {
            .tv_sec = (time_t)(remaining / 1000),
            .tv_nsec = (long)((remaining % 1000) * 1000000L)
        };
        if (nanosleep(&delay, &delay) == 0) return;
        if (errno != EINTR) return;
    }
}

static int wait_fd(int fd, short events, int64_t deadline) {
    for (;;) {
        int64_t remaining = deadline - now_ms();
        if (remaining <= 0) return 0;
        struct pollfd pfd = { .fd = fd, .events = events, .revents = 0 };
        int timeout = remaining > INT32_MAX ? INT32_MAX : (int)remaining;
        int rc = poll(&pfd, 1, timeout);
        if (rc > 0) return (pfd.revents & (events | POLLERR | POLLHUP | POLLNVAL)) != 0 ? 1 : -1;
        if (rc == 0) return 0;
        if (errno != EINTR) return -1;
    }
}

/* Returns -2 for deadline expiry, -1 for I/O error, and zero for EOF. */
static ssize_t recv_until(int fd, void *buffer, size_t capacity, int64_t deadline) {
    for (;;) {
        int ready = wait_fd(fd, POLLIN, deadline);
        if (ready <= 0) return ready == 0 ? -2 : -1;
        ssize_t n = recv(fd, buffer, capacity, 0);
        if (n >= 0) return n;
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return -1;
    }
}

static int send_all(int fd, const void *buffer, size_t length, int64_t deadline) {
    const unsigned char *p = (const unsigned char *)buffer;
    size_t sent = 0U;
    while (sent < length) {
        if (wait_fd(fd, POLLOUT, deadline) <= 0) return -1;
#ifdef MSG_NOSIGNAL
        ssize_t n = send(fd, p + sent, length - sent, MSG_NOSIGNAL);
#else
        ssize_t n = send(fd, p + sent, length - sent, 0);
#endif
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        } else if (n == 0) {
            return -1;
        }
    }
    return 0;
}

static void set_socket_options(int fd) {
#ifdef SO_NOSIGPIPE
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)fd;
#endif
}

static int connect_backend(unsigned short port, int64_t deadline) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    set_socket_options(fd);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        close(fd);
        return -1;
    }
    int rc = connect(fd, (struct sockaddr *)&address, sizeof(address));
    if (rc < 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }
    if (rc < 0) {
        if (wait_fd(fd, POLLOUT, deadline) <= 0) {
            close(fd);
            return -1;
        }
        int error = 0;
        socklen_t error_length = (socklen_t)sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_length) != 0 || error != 0) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static bool header_done(const unsigned char *buffer, size_t length, size_t *header_end) {
    if (length < 4U) return false;
    for (size_t i = 3U; i < length; ++i) {
        if (buffer[i - 3U] == '\r' && buffer[i - 2U] == '\n' &&
            buffer[i - 1U] == '\r' && buffer[i] == '\n') {
            if (header_end != NULL) *header_end = i + 1U;
            return true;
        }
    }
    return false;
}

static void generated_error(int fd, int status, const char *reason, const char *body, int64_t deadline) {
    char response[512];
    size_t body_length = strlen(body);
    int length = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
        status, reason, body_length, body);
    if (length > 0 && (size_t)length < sizeof(response)) {
        (void)send_all(fd, response, (size_t)length, deadline);
    }
}

static int read_request(int fd, unsigned char *buffer, size_t *length, int64_t deadline) {
    *length = 0U;
    while (*length < EM_REQUEST_HEADER_MAX) {
        ssize_t n = recv_until(fd, buffer + *length, EM_REQUEST_HEADER_MAX - *length, deadline);
        if (n == -2) return 408;
        if (n <= 0) return 400;
        *length += (size_t)n;
        if (header_done(buffer, *length, NULL)) return 0;
    }
    size_t first_line = 0U;
    while (first_line + 1U < *length &&
           !(buffer[first_line] == '\r' && buffer[first_line + 1U] == '\n')) {
        ++first_line;
    }
    return first_line > EM_REQUEST_LINE_MAX ? 414 : 431;
}

/* Validates a bounded upstream response and extracts its optional Content-Length.
 * The proxy buffers an upstream response before writing it to a client so a retry
 * never occurs after client response bytes have started. */
static bool valid_upstream_headers(const unsigned char *data, size_t header_end,
                                  long long *content_length) {
    if (header_end < 12U || header_end > EM_RESPONSE_HEADER_MAX) return false;
    const unsigned char *line_end = NULL;
    for (size_t i = 1U; i < header_end; ++i) {
        if (data[i - 1U] == '\r' && data[i] == '\n') {
            line_end = data + i - 1U;
            break;
        }
    }
    if (line_end == NULL) return false;
    size_t status_length = (size_t)(line_end - data);
    if (status_length < 12U ||
        (memcmp(data, "HTTP/1.0 ", 9U) != 0 && memcmp(data, "HTTP/1.1 ", 9U) != 0) ||
        data[9] < '2' || data[9] > '5' ||
        data[10] < '0' || data[10] > '9' ||
        data[11] < '0' || data[11] > '9' ||
        (status_length > 12U && data[12] != ' ')) return false;

    unsigned content_length_count = 0U;
    bool transfer_encoding = false;
    *content_length = -1LL;
    const unsigned char *p = line_end + 2;
    const unsigned char *limit = data + header_end - 2U;
    while (p < limit) {
        const unsigned char *eol = NULL;
        for (const unsigned char *q = p; q + 1 < data + header_end; ++q) {
            if (q[0] == '\r' && q[1] == '\n') {
                eol = q;
                break;
            }
        }
        if (eol == NULL) return false;
        size_t line_length = (size_t)(eol - p);
        if (line_length == 0U) break;
        if (*p == ' ' || *p == '\t') return false;
        const unsigned char *colon = memchr(p, ':', line_length);
        if (colon == NULL || colon == p) return false;
        size_t name_length = (size_t)(colon - p);
        for (size_t i = 0U; i < name_length; ++i) {
            unsigned char c = p[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", (int)c) != NULL)) {
                return false;
            }
        }
        const unsigned char *value = colon + 1;
        const unsigned char *value_end = eol;
        while (value < value_end && (*value == ' ' || *value == '\t')) ++value;
        while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) --value_end;
        for (const unsigned char *q = value; q < value_end; ++q) {
            if ((*q < 0x20U && *q != '\t') || *q == 0x7fU) return false;
        }
        if (name_length == 17U && strncasecmp((const char *)p, "Transfer-Encoding", 17U) == 0) {
            transfer_encoding = true;
        }
        if (name_length == 14U && strncasecmp((const char *)p, "Content-Length", 14U) == 0) {
            if (++content_length_count > 1U || value == value_end) return false;
            unsigned long long parsed = 0ULL;
            for (const unsigned char *q = value; q < value_end; ++q) {
                if (*q < '0' || *q > '9') return false;
                unsigned digit = (unsigned)(*q - '0');
                if (parsed > (unsigned long long)EM_RESPONSE_BODY_MAX / 10ULL ||
                    parsed * 10ULL + digit > (unsigned long long)EM_RESPONSE_BODY_MAX) return false;
                parsed = parsed * 10ULL + digit;
            }
            *content_length = (long long)parsed;
        }
        p = eol + 2;
    }
    return !transfer_encoding;
}

static bool header_name_equals(const unsigned char *name, size_t length, const char *literal) {
    return strlen(literal) == length && strncasecmp((const char *)name, literal, length) == 0;
}

static bool header_name_is_token(const unsigned char *name, size_t length) {
    if (length == 0U) return false;
    for (size_t i = 0U; i < length; ++i) {
        unsigned char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", (int)c) != NULL)) return false;
    }
    return true;
}

static bool is_standard_hop_header(const unsigned char *name, size_t length) {
    static const char *const names[] = {
        "Connection", "Keep-Alive", "Proxy-Authenticate", "Proxy-Authorization",
        "Proxy-Connection", "TE", "Trailer", "Transfer-Encoding", "Upgrade"
    };
    for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (header_name_equals(name, length, names[i])) return true;
    }
    return false;
}

static bool collect_connection_tokens(const unsigned char *response, size_t header_end,
                                      char nominated[32][64], size_t *nominated_count) {
    *nominated_count = 0U;
    const unsigned char *line_end = NULL;
    for (size_t i = 1U; i < header_end; ++i) {
        if (response[i - 1U] == '\r' && response[i] == '\n') {
            line_end = response + i - 1U;
            break;
        }
    }
    if (line_end == NULL) return false;
    const unsigned char *p = line_end + 2;
    const unsigned char *limit = response + header_end - 2U;
    while (p < limit) {
        const unsigned char *eol = NULL;
        for (const unsigned char *q = p; q + 1U < response + header_end; ++q) {
            if (q[0] == '\r' && q[1] == '\n') { eol = q; break; }
        }
        if (eol == NULL) return false;
        size_t line_length = (size_t)(eol - p);
        if (line_length == 0U) break;
        const unsigned char *colon = memchr(p, ':', line_length);
        if (colon == NULL) return false;
        size_t name_length = (size_t)(colon - p);
        if (header_name_equals(p, name_length, "Connection")) {
            const unsigned char *item = colon + 1;
            const unsigned char *item_end = eol;
            while (item < item_end && (*item == ' ' || *item == '\t')) ++item;
            while (item_end > item && (item_end[-1] == ' ' || item_end[-1] == '\t')) --item_end;
            if (item == item_end) return false;
            while (item < item_end) {
                const unsigned char *comma = memchr(item, ',', (size_t)(item_end - item));
                const unsigned char *token_end = comma != NULL ? comma : item_end;
                while (item < token_end && (*item == ' ' || *item == '\t')) ++item;
                while (token_end > item && (token_end[-1] == ' ' || token_end[-1] == '\t')) --token_end;
                size_t token_length = (size_t)(token_end - item);
                if (token_length == 0U || token_length >= sizeof(nominated[0]) ||
                    *nominated_count >= 32U || !header_name_is_token(item, token_length)) return false;
                memcpy(nominated[*nominated_count], item, token_length);
                nominated[*nominated_count][token_length] = '\0';
                ++*nominated_count;
                if (comma == NULL) break;
                item = comma + 1;
                if (item == item_end) return false;
            }
        }
        p = eol + 2;
    }
    return true;
}

/* Remove hop-by-hop fields (including fields nominated by Connection), preserve
 * end-to-end headers/body, and make the client-side close semantics explicit. */
static bool sanitize_upstream_response(unsigned char *response, size_t capacity,
                                       size_t *response_length) {
    size_t original_header_end = 0U;
    if (!header_done(response, *response_length, &original_header_end) ||
        original_header_end > EM_RESPONSE_HEADER_MAX) return false;

    char nominated[32][64];
    size_t nominated_count = 0U;
    if (!collect_connection_tokens(response, original_header_end, nominated, &nominated_count)) return false;

    const unsigned char *status_end = NULL;
    for (size_t i = 1U; i < original_header_end; ++i) {
        if (response[i - 1U] == '\r' && response[i] == '\n') {
            status_end = response + i - 1U;
            break;
        }
    }
    if (status_end == NULL) return false;

    unsigned char header_out[EM_RESPONSE_HEADER_MAX + 256U];
    size_t out_used = (size_t)(status_end - response) + 2U;
    memcpy(header_out, response, out_used);
    const unsigned char *p = status_end + 2;
    const unsigned char *limit = response + original_header_end - 2U;
    while (p < limit) {
        const unsigned char *eol = NULL;
        for (const unsigned char *q = p; q + 1U < response + original_header_end; ++q) {
            if (q[0] == '\r' && q[1] == '\n') { eol = q; break; }
        }
        if (eol == NULL) return false;
        size_t line_length = (size_t)(eol - p);
        if (line_length == 0U) break;
        const unsigned char *colon = memchr(p, ':', line_length);
        if (colon == NULL) return false;
        size_t name_length = (size_t)(colon - p);
        if (!header_name_is_token(p, name_length)) return false;

        bool skip = is_standard_hop_header(p, name_length);
        for (size_t i = 0U; !skip && i < nominated_count; ++i) {
            if (header_name_equals(p, name_length, nominated[i])) skip = true;
        }
        size_t complete_line_length = line_length + 2U;
        if (!skip) {
            if (out_used + complete_line_length + 21U > sizeof(header_out)) return false;
            memcpy(header_out + out_used, p, complete_line_length);
            out_used += complete_line_length;
        }
        p = eol + 2;
    }

    static const unsigned char close_header[] = "Connection: close\r\n\r\n";
    size_t body_length = *response_length - original_header_end;
    if (body_length > EM_RESPONSE_BODY_MAX || out_used + sizeof(close_header) - 1U + body_length > capacity) return false;
    memcpy(header_out + out_used, close_header, sizeof(close_header) - 1U);
    out_used += sizeof(close_header) - 1U;
    memmove(response + out_used, response + original_header_end, body_length);
    memcpy(response, header_out, out_used);
    *response_length = out_used + body_length;
    return true;
}

static bool read_upstream_response(int fd, int64_t deadline, unsigned char *response,
                                   size_t capacity, size_t *response_length) {
    size_t used = 0U;
    size_t header_end = 0U;
    long long content_length = -1LL;
    bool headers_validated = false;
    bool eof = false;

    while (used < capacity) {
        if (headers_validated && content_length >= 0LL &&
            used >= header_end + (size_t)content_length) {
            *response_length = header_end + (size_t)content_length;
            return true;
        }
        ssize_t n = recv_until(fd, response + used, capacity - used, deadline);
        if (n == 0) {
            eof = true;
            break;
        }
        if (n < 0) return false;
        used += (size_t)n;

        if (!headers_validated && header_done(response, used, &header_end)) {
            if (header_end > EM_RESPONSE_HEADER_MAX ||
                !valid_upstream_headers(response, header_end, &content_length)) return false;
            headers_validated = true;
        } else if (!headers_validated && used >= EM_RESPONSE_HEADER_MAX) {
            return false;
        }
        if (headers_validated && used >= header_end &&
            used - header_end > EM_RESPONSE_BODY_MAX) return false;
        if (headers_validated && content_length >= 0LL &&
            used > header_end + (size_t)content_length) {
            /* Extra bytes after a declared body make the upstream message ambiguous. */
            return false;
        }
    }
    if (!eof || !headers_validated) return false;
    if (content_length >= 0LL) {
        if (used < header_end + (size_t)content_length) return false;
        *response_length = header_end + (size_t)content_length;
        return true;
    }
    *response_length = used;
    return true;
}

static bool probe_backend(unsigned short port) {
    int64_t deadline = now_ms() + HEALTH_TIMEOUT_MS;
    int fd = connect_backend(port, deadline);
    if (fd < 0) return false;
    static const char request[] = "GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if (send_all(fd, request, sizeof(request) - 1U, deadline) != 0) {
        close(fd);
        return false;
    }

    unsigned char response[HEALTH_RESPONSE_MAX + 1U];
    size_t used = 0U;
    bool complete = false;
    while (used < sizeof(response)) {
        ssize_t n = recv_until(fd, response + used, sizeof(response) - used, deadline);
        if (n == 0) { complete = true; break; }
        if (n < 0) break;
        used += (size_t)n;
    }
    close(fd);
    if (!complete || used == 0U || used > HEALTH_RESPONSE_MAX) return false;

    size_t header_end = 0U;
    if (!header_done(response, used, &header_end) || header_end > 1024U ||
        used - header_end > 1024U) return false;
    const unsigned char *line_end = NULL;
    for (size_t i = 1U; i < header_end; ++i) {
        if (response[i - 1U] == '\r' && response[i] == '\n') {
            line_end = response + i - 1U;
            break;
        }
    }
    if (line_end == NULL || (size_t)(line_end - response) < 12U ||
        (memcmp(response, "HTTP/1.0 ", 9U) != 0 && memcmp(response, "HTTP/1.1 ", 9U) != 0) ||
        response[9] < '2' || response[9] > '2' ||
        response[10] < '0' || response[10] > '9' ||
        response[11] < '0' || response[11] > '9') return false;

    long long expected_length = -1LL;
    const unsigned char *p = line_end + 2;
    const unsigned char *limit = response + header_end - 2U;
    while (p < limit) {
        const unsigned char *eol = NULL;
        for (const unsigned char *q = p; q + 1 < response + header_end; ++q) {
            if (q[0] == '\r' && q[1] == '\n') { eol = q; break; }
        }
        if (eol == NULL) return false;
        const unsigned char *colon = memchr(p, ':', (size_t)(eol - p));
        if (colon == NULL) return false;
        size_t name_length = (size_t)(colon - p);
        if (name_length == 14U && strncasecmp((const char *)p, "Content-Length", 14U) == 0) {
            const unsigned char *v = colon + 1;
            while (v < eol && (*v == ' ' || *v == '\t')) ++v;
            if (v == eol) return false;
            long long value = 0LL;
            for (; v < eol; ++v) {
                if (*v < '0' || *v > '9') return false;
                value = value * 10LL + (long long)(*v - '0');
                if (value > 1024LL) return false;
            }
            if (expected_length >= 0LL) return false;
            expected_length = value;
        }
        p = eol + 2;
    }
    if (expected_length >= 0LL && (size_t)expected_length != used - header_end) return false;
    return true;
}

static int64_t wall_now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000L);
}

static void *health_worker(void *unused) {
    (void)unused;
    int64_t next_cycle = now_ms();
    for (;;) {
        for (size_t i = 0U; i < EM_BACKEND_COUNT; ++i) {
            em_backend_record backend;
            if (em_backend_pool_snapshot(&g_pool, i, &backend) == 0) {
                int64_t probe_started = now_ms();
                bool success = probe_backend(backend.port);
                double elapsed_ms = (double)(now_ms() - probe_started);
                em_backend_pool_probe_result(&g_pool, i, success, wall_now_ms(), elapsed_ms);
            }
        }
        next_cycle += HEALTH_INTERVAL_MS;
        if (next_cycle < now_ms()) next_cycle = now_ms();
        sleep_until_ms(next_cycle);
    }
    return NULL;
}

static void process_client(int client, int64_t accepted_at) {
    unsigned char request[EM_REQUEST_HEADER_MAX];
    size_t request_length = 0U;
    int status = read_request(client, request, &request_length, accepted_at + IO_TIMEOUT_MS);
    if (status != 0) {
        const char *reason = status == 408 ? "Request Timeout" :
                             status == 414 ? "URI Too Long" :
                             status == 431 ? "Request Header Fields Too Large" : "Bad Request";
        const char *body = status == 408 ? "request timeout\n" :
                           status == 414 ? "request line too long\n" :
                           status == 431 ? "headers too large\n" : "bad request\n";
        generated_error(client, status, reason, body, accepted_at + REQUEST_DEADLINE_MS);
        return;
    }

    em_request parsed;
    status = em_parse_request(request, request_length, &parsed);
    if (status != 0) {
        const char *reason = "Bad Request";
        const char *body = "bad request\n";
        if (status == 405) { reason = "Method Not Allowed"; body = "method not allowed\n"; }
        else if (status == 414) { reason = "URI Too Long"; body = "request line too long\n"; }
        else if (status == 413) { reason = "Content Too Large"; body = "request body not supported\n"; }
        else if (status == 431) { reason = "Request Header Fields Too Large"; body = "headers too large\n"; }
        else if (status == 505) { reason = "HTTP Version Not Supported"; body = "HTTP version not supported\n"; }

        if (status == 405) {
            char response[512];
            size_t body_length = strlen(body);
            int length = snprintf(response, sizeof(response),
                "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET\r\n"
                "Content-Type: text/plain; charset=utf-8\r\nContent-Length: %zu\r\n"
                "Connection: close\r\n\r\n%s", body_length, body);
            if (length > 0 && (size_t)length < sizeof(response)) {
                (void)send_all(client, response, (size_t)length, accepted_at + REQUEST_DEADLINE_MS);
            }
        } else {
            generated_error(client, status, reason, body, accepted_at + REQUEST_DEADLINE_MS);
        }
        return;
    }

    bool attempted[EM_BACKEND_COUNT] = { false, false, false };
    size_t attempts = 0U;
    int64_t request_deadline = accepted_at + REQUEST_DEADLINE_MS;
    while (attempts < EM_BACKEND_COUNT) {
        int selected = em_backend_pool_select_reserve(&g_pool, attempted);
        if (selected < 0) {
            generated_error(client, attempts == 0U ? 503 : 502,
                            attempts == 0U ? "Service Unavailable" : "Bad Gateway",
                            attempts == 0U ? "no healthy backend\n" : "upstream unavailable\n",
                            request_deadline);
            return;
        }
        size_t index = (size_t)selected;
        attempted[index] = true;
        ++attempts;

        em_backend_record backend;
        if (em_backend_pool_snapshot(&g_pool, index, &backend) != 0) {
            em_backend_pool_release(&g_pool, index);
            generated_error(client, 502, "Bad Gateway", "upstream failure\n", request_deadline);
            return;
        }
        int64_t attempt_deadline = now_ms() + IO_TIMEOUT_MS;
        if (attempt_deadline > request_deadline - 1000) attempt_deadline = request_deadline - 1000;
        if (attempt_deadline <= now_ms()) {
            em_backend_pool_record_failure(&g_pool, index);
            em_backend_pool_release(&g_pool, index);
            break;
        }

        int upstream = connect_backend(backend.port, attempt_deadline);
        bool attempt_succeeded = false;
        unsigned char *response = NULL;
        size_t response_length = 0U;
        if (upstream >= 0) {
            char forwarded[EM_REQUEST_HEADER_MAX + 256U];
            int forward_length = snprintf(forwarded, sizeof(forwarded),
                "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nConnection: close\r\n\r\n",
                parsed.target, (unsigned)backend.port);
            if (forward_length > 0 && (size_t)forward_length < sizeof(forwarded) &&
                send_all(upstream, forwarded, (size_t)forward_length, attempt_deadline) == 0) {
                size_t capacity = EM_RESPONSE_HEADER_MAX + EM_RESPONSE_BODY_MAX + 256U;
                response = malloc(capacity);
                if (response != NULL) {
                    attempt_succeeded = read_upstream_response(upstream, attempt_deadline,
                        response, capacity, &response_length);
                    if (attempt_succeeded) {
                        attempt_succeeded = sanitize_upstream_response(response, capacity, &response_length);
                    }
                }
            }
            close(upstream);
        }

        if (!attempt_succeeded) {
            free(response);
            em_backend_pool_record_failure(&g_pool, index);
            em_backend_pool_release(&g_pool, index);
            continue;
        }

        /* Buffering means no response byte has reached the client before this point;
         * downstream write errors are client disconnects and must not trigger retry. */
        (void)send_all(client, response, response_length, request_deadline);
        free(response);
        em_backend_pool_release(&g_pool, index);
        return;
    }

    generated_error(client, 502, "Bad Gateway", "upstream failure\n", request_deadline);
}

static void *client_worker(void *argument) {
    client_work *work = (client_work *)argument;
    int client = work->client_fd;
    int64_t accepted_at = work->accepted_at_ms;
    free(work);

    process_client(client, accepted_at);
    close(client);

    (void)pthread_mutex_lock(&g_capacity_mutex);
    if (g_active_clients > 0U) --g_active_clients;
    (void)pthread_mutex_unlock(&g_capacity_mutex);
    return NULL;
}

int em_proxy_run(unsigned short listen_port, unsigned short backend_base_port, em_policy policy) {
    if (em_backend_pool_init(&g_pool, backend_base_port, policy) != 0) return -1;

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        em_backend_pool_destroy(&g_pool);
        return -1;
    }
    int one = 1;
    (void)setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(listen_port);
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1 ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, LISTEN_BACKLOG) != 0) {
        close(listener);
        em_backend_pool_destroy(&g_pool);
        return -1;
    }

    pthread_t health_thread;
    if (pthread_create(&health_thread, NULL, health_worker, NULL) != 0) {
        close(listener);
        em_backend_pool_destroy(&g_pool);
        return -1;
    }
    (void)pthread_detach(health_thread);
    (void)signal(SIGPIPE, SIG_IGN);

    for (;;) {
        int client = accept(listener, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            close(listener);
            return -1;
        }
        set_socket_options(client);
        int flags = fcntl(client, F_GETFL, 0);
        if (flags < 0 || fcntl(client, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(client);
            continue;
        }

        (void)pthread_mutex_lock(&g_capacity_mutex);
        bool at_capacity = g_active_clients >= MAX_PROXY_CONNECTIONS;
        if (!at_capacity) ++g_active_clients;
        (void)pthread_mutex_unlock(&g_capacity_mutex);
        if (at_capacity) {
            close(client);
            continue;
        }

        client_work *work = malloc(sizeof(*work));
        if (work == NULL) {
            close(client);
            (void)pthread_mutex_lock(&g_capacity_mutex);
            --g_active_clients;
            (void)pthread_mutex_unlock(&g_capacity_mutex);
            continue;
        }
        work->client_fd = client;
        work->accepted_at_ms = now_ms();
        pthread_t worker;
        if (pthread_create(&worker, NULL, client_worker, work) != 0) {
            free(work);
            close(client);
            (void)pthread_mutex_lock(&g_capacity_mutex);
            --g_active_clients;
            (void)pthread_mutex_unlock(&g_capacity_mutex);
            continue;
        }
        (void)pthread_detach(worker);
    }
}
