/*
 * src/admin_api/handler.c
 *
 * handle_admin_request(client_fd): reads one HTTP request, dispatches to
 * the appropriate admin API route, writes the response, closes client_fd.
 *
 * Routes (all under /api/v1/):
 *   GET  /health
 *   GET  /overview
 *   GET  /backends
 *   PUT  /policy
 *   GET  /metrics[?window_seconds=N]
 *   GET  /benchmarks/latest
 *   OPTIONS <any>  — CORS preflight
 */

#define _POSIX_C_SOURCE 200809L

#include "edgemesh/admin_handler.h"
#include "edgemesh/json_builder.h"
#include "edgemesh/io_utils.h"
#include "edgemesh/http_response.h"
#include "edgemesh/http_parser.h"
#include "edgemesh/backend.h"
#include "edgemesh/log.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

/* Timeout constants from architecture.md §5a. */
#define ADMIN_READ_TIMEOUT_MS  (10 * 1000)  /* 10 s */
#define ADMIN_MAX_HEADER_SIZE  16384         /* 16 KiB */
#define ADMIN_MAX_BODY_SIZE    4096          /* 4 KiB  */

/* Allowed CORS origins. */
static const char * const CORS_ORIGINS[] = {
    "http://localhost:5173",
    "http://127.0.0.1:5173",
    NULL
};

/* -----------------------------------------------------------------------
 * Internal: find origin in whitelist; returns it or NULL.
 * ----------------------------------------------------------------------- */
static const char *match_cors_origin(const char *origin) {
    if (!origin || origin[0] == '\0') return NULL;
    for (int i = 0; CORS_ORIGINS[i] != NULL; i++) {
        if (strcmp(origin, CORS_ORIGINS[i]) == 0) {
            return CORS_ORIGINS[i];
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Internal: find a header value by name (case-insensitive) in a parsed
 * request; returns pointer to the value or "" if not found.
 * ----------------------------------------------------------------------- */
static const char *find_header(const http_request_t *req, const char *name) {
    for (int i = 0; i < req->header_count; i++) {
        /* Case-insensitive compare of header name. */
        const char *a = req->headers[i].name;
        const char *b = name;
        int match = 1;
        while (*a && *b) {
            char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
            char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
            if (ca != cb) { match = 0; break; }
            a++; b++;
        }
        if (match && *a == '\0' && *b == '\0') {
            return req->headers[i].value;
        }
    }
    return "";
}

/* -----------------------------------------------------------------------
 * Internal: send_json_response — sends a JSON body with CORS headers.
 * origin_echo is the matched CORS origin (may be NULL).
 * ----------------------------------------------------------------------- */
static void send_json_response(int fd, int status, const char *status_text,
                               const char *json_body,
                               const char *origin_echo) {
    /* Build extra headers array. */
    http_header_t extra[4];
    int nextra = 0;

    strncpy(extra[nextra].name,  "Content-Type",
            sizeof(extra[nextra].name)  - 1);
    strncpy(extra[nextra].value, "application/json; charset=utf-8",
            sizeof(extra[nextra].value) - 1);
    extra[nextra].name[sizeof(extra[nextra].name)-1]   = '\0';
    extra[nextra].value[sizeof(extra[nextra].value)-1] = '\0';
    nextra++;

    if (origin_echo) {
        strncpy(extra[nextra].name,  "Access-Control-Allow-Origin",
                sizeof(extra[nextra].name)  - 1);
        strncpy(extra[nextra].value, origin_echo,
                sizeof(extra[nextra].value) - 1);
        extra[nextra].name[sizeof(extra[nextra].name)-1]   = '\0';
        extra[nextra].value[sizeof(extra[nextra].value)-1] = '\0';
        nextra++;

        strncpy(extra[nextra].name,  "Vary",
                sizeof(extra[nextra].name)  - 1);
        strncpy(extra[nextra].value, "Origin",
                sizeof(extra[nextra].value) - 1);
        extra[nextra].name[sizeof(extra[nextra].name)-1]   = '\0';
        extra[nextra].value[sizeof(extra[nextra].value)-1] = '\0';
        nextra++;
    }

    size_t body_len = json_body ? strlen(json_body) : 0;
    send_response(fd, status, status_text,
                  extra, nextra,
                  json_body, body_len);
}

/* -----------------------------------------------------------------------
 * Internal: send OPTIONS preflight response.
 * ----------------------------------------------------------------------- */
static void send_preflight(int fd, const char *origin_echo) {
    http_header_t extra[5];
    int nextra = 0;

    if (origin_echo) {
        strncpy(extra[nextra].name,  "Access-Control-Allow-Origin",
                sizeof(extra[nextra].name)  - 1);
        strncpy(extra[nextra].value, origin_echo,
                sizeof(extra[nextra].value) - 1);
        extra[nextra].name[sizeof(extra[nextra].name)-1]   = '\0';
        extra[nextra].value[sizeof(extra[nextra].value)-1] = '\0';
        nextra++;

        strncpy(extra[nextra].name,  "Vary",
                sizeof(extra[nextra].name)  - 1);
        strncpy(extra[nextra].value, "Origin",
                sizeof(extra[nextra].value) - 1);
        extra[nextra].name[sizeof(extra[nextra].name)-1]   = '\0';
        extra[nextra].value[sizeof(extra[nextra].value)-1] = '\0';
        nextra++;
    }

    strncpy(extra[nextra].name,  "Access-Control-Allow-Methods",
            sizeof(extra[nextra].name)  - 1);
    strncpy(extra[nextra].value, "GET, PUT, OPTIONS",
            sizeof(extra[nextra].value) - 1);
    extra[nextra].name[sizeof(extra[nextra].name)-1]   = '\0';
    extra[nextra].value[sizeof(extra[nextra].value)-1] = '\0';
    nextra++;

    strncpy(extra[nextra].name,  "Access-Control-Allow-Headers",
            sizeof(extra[nextra].name)  - 1);
    strncpy(extra[nextra].value, "Content-Type",
            sizeof(extra[nextra].value) - 1);
    extra[nextra].name[sizeof(extra[nextra].name)-1]   = '\0';
    extra[nextra].value[sizeof(extra[nextra].value)-1] = '\0';
    nextra++;

    send_response(fd, 200, "OK", extra, nextra, "", 0);
}

/* -----------------------------------------------------------------------
 * Internal: read the request body up to ADMIN_MAX_BODY_SIZE.
 * Content-Length is obtained from the parsed request headers.
 * hdr_buf/hdr_len: the buffer already read by read_with_timeout, which
 * may contain body bytes that follow the \r\n\r\n header boundary.
 * Returns bytes read, -1 on error, -413 if body exceeds limit.
 * ----------------------------------------------------------------------- */
static ssize_t read_body(int fd, const http_request_t *req,
                         const char *hdr_buf, size_t hdr_len,
                         char *body_buf, size_t body_bufsz) {
    const char *cl_val = find_header(req, "Content-Length");
    if (cl_val[0] == '\0') {
        /* No Content-Length — treat as empty body. */
        return 0;
    }

    char *endp;
    long content_len = strtol(cl_val, &endp, 10);
    if (*endp != '\0' || content_len < 0) {
        return -1;
    }

    if ((size_t)content_len > ADMIN_MAX_BODY_SIZE) {
        return -413;
    }

    if ((size_t)content_len >= body_bufsz) {
        return -413;
    }

    size_t total = 0;

    /* Copy any body bytes already buffered in hdr_buf after \r\n\r\n. */
    const char *sep = NULL;
    for (size_t i = 0; i + 3 < hdr_len; i++) {
        if (hdr_buf[i]   == '\r' && hdr_buf[i+1] == '\n' &&
            hdr_buf[i+2] == '\r' && hdr_buf[i+3] == '\n') {
            sep = hdr_buf + i + 4;
            break;
        }
    }
    if (sep != NULL) {
        size_t available = (size_t)(hdr_buf + hdr_len - sep);
        size_t to_copy   = available < (size_t)content_len
                           ? available : (size_t)content_len;
        if (to_copy > 0) {
            memcpy(body_buf, sep, to_copy);
            total = to_copy;
        }
    }

    /* Read remaining bytes from the socket. */
    while (total < (size_t)content_len) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(fd, &rset);
        struct timeval tv;
        tv.tv_sec  = ADMIN_READ_TIMEOUT_MS / 1000;
        tv.tv_usec = (ADMIN_READ_TIMEOUT_MS % 1000) * 1000;
        int rc = select(fd + 1, &rset, NULL, NULL, &tv);
        if (rc <= 0) return -1;

        ssize_t n = read(fd, body_buf + total, (size_t)content_len - total);
        if (n <= 0) return -1;
        total += (size_t)n;
    }

    body_buf[total] = '\0';
    return (ssize_t)total;
}

/* -----------------------------------------------------------------------
 * Internal: minimal JSON string extractor.
 *
 * Finds the value of a top-level string field named `key` in `json`.
 * Writes the raw (unescaped) value into out[0..out_size-1].
 * Returns 1 on success, 0 if not found or not a string.
 * ----------------------------------------------------------------------- */
static int json_extract_string(const char *json, const char *key,
                               char *out, size_t out_size) {
    /* Build search pattern: "key": " */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return 0;
    p += strlen(pattern);

    /* Skip whitespace and colon. */
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;

    size_t wi = 0;
    while (*p != '\0' && *p != '"') {
        if (wi + 1 >= out_size) return 0;
        if (*p == '\\') {
            p++;
            if (*p == '\0') return 0;
            /* We only need basic unescaping for policy values. */
            out[wi++] = *p;
        } else {
            out[wi++] = *p;
        }
        p++;
    }
    if (*p != '"') return 0;
    out[wi] = '\0';
    return 1;
}

/* -----------------------------------------------------------------------
 * handle_admin_request
 * ----------------------------------------------------------------------- */
void handle_admin_request(int client_fd) {
    char hdr_buf[ADMIN_MAX_HEADER_SIZE];
    char body_buf[ADMIN_MAX_BODY_SIZE + 1];

    /* Read request headers with timeout. */
    ssize_t hlen = read_with_timeout(client_fd, hdr_buf, sizeof(hdr_buf),
                                     ADMIN_READ_TIMEOUT_MS);
    if (hlen <= 0) {
        close(client_fd);
        return;
    }

    /* Parse the request.
     * The shared HTTP parser only accepts GET; the admin API also accepts PUT
     * and OPTIONS.  When the parser returns PARSE_METHOD_NOT_ALLOWED we
     * attempt a minimal manual parse of the first line so we can dispatch
     * correctly.
     */
    http_request_t req;
    memset(&req, 0, sizeof(req));
    parse_result_t pr = parse_request(hdr_buf, (size_t)hlen, &req);

    /* If the parser rejected the method, try to extract it manually. */
    if (pr == PARSE_METHOD_NOT_ALLOWED) {
        /* hdr_buf starts with "METHOD SP target SP version\r\n…" */
        const char *sp = (const char *)memchr(hdr_buf, ' ', (size_t)hlen);
        if (sp != NULL) {
            size_t mlen = (size_t)(sp - hdr_buf);
            if (mlen < sizeof(req.method)) {
                memcpy(req.method, hdr_buf, mlen);
                req.method[mlen] = '\0';
            }
            /* Extract target (between first and second space). */
            const char *t_start = sp + 1;
            const char *sp2 = (const char *)memchr(t_start, ' ',
                                  (size_t)(hdr_buf + hlen - t_start));
            if (sp2 != NULL) {
                size_t tlen = (size_t)(sp2 - t_start);
                if (tlen < sizeof(req.target)) {
                    memcpy(req.target, t_start, tlen);
                    req.target[tlen] = '\0';
                }
            }
            /* Re-parse headers from the remainder so find_header() works. */
            /* We only need Origin; do a lightweight manual scan. */
            const char *hl = strstr(hdr_buf, "\r\n");
            if (hl) {
                hl += 2; /* skip request line CRLF */
                /* Reuse the parser on a synthetic GET line + rest of headers
                 * so req.headers is populated correctly. */
                char fake_buf[ADMIN_MAX_HEADER_SIZE];
                int flen = snprintf(fake_buf, sizeof(fake_buf),
                                    "GET %s HTTP/1.1\r\n%s",
                                    req.target[0] ? req.target : "/",
                                    hl);
                if (flen > 0 && (size_t)flen < sizeof(fake_buf)) {
                    http_request_t fake_req;
                    memset(&fake_req, 0, sizeof(fake_req));
                    if (parse_request(fake_buf, (size_t)flen, &fake_req)
                            == PARSE_OK) {
                        /* Copy header array; keep method/target we set. */
                        req.header_count = fake_req.header_count;
                        memcpy(req.headers, fake_req.headers,
                               (size_t)fake_req.header_count
                               * sizeof(http_header_t));
                    }
                }
            }
            /* Treat as successfully parsed for routing purposes. */
            pr = PARSE_OK;
        }
    }

    /* Determine matched CORS origin. */
    const char *origin     = find_header(&req, "Origin");
    const char *origin_echo = match_cors_origin(origin);

    /* Handle parse failures for admin API. */
    if (pr == PARSE_BAD_REQUEST) {
        const char *err =
            "{\"error\":{\"code\":\"BAD_REQUEST\","
            "\"message\":\"malformed request\"}}";
        send_json_response(client_fd, 400, "Bad Request",
                           err, origin_echo);
        close(client_fd);
        return;
    }
    if (pr == PARSE_HEADER_TOO_LARGE) {
        const char *err =
            "{\"error\":{\"code\":\"HEADER_TOO_LARGE\","
            "\"message\":\"request headers exceed 16 KiB\"}}";
        send_json_response(client_fd, 431, "Request Header Fields Too Large",
                           err, origin_echo);
        close(client_fd);
        return;
    }

    /* Method check — admin accepts GET, PUT, OPTIONS. */
    int is_get     = (strcmp(req.method, "GET")     == 0);
    int is_put     = (strcmp(req.method, "PUT")     == 0);
    int is_options = (strcmp(req.method, "OPTIONS") == 0);

    /* Handle OPTIONS preflight for any route. */
    if (is_options) {
        send_preflight(client_fd, origin_echo);
        close(client_fd);
        return;
    }

    /* Strip query string from path for route matching. */
    char path[sizeof(req.target)];
    strncpy(path, req.target, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    char *qs = strchr(path, '?');
    if (qs) *qs = '\0';

    /* ------------------------------------------------------------------ */
    /* Route: GET /api/v1/health                                           */
    /* ------------------------------------------------------------------ */
    if (strcmp(path, "/api/v1/health") == 0) {
        if (!is_get) {
            const char *err =
                "{\"error\":{\"code\":\"METHOD_NOT_ALLOWED\","
                "\"message\":\"use GET\"}}";
            send_json_response(client_fd, 405, "Method Not Allowed",
                               err, origin_echo);
            close(client_fd);
            return;
        }
        char *body = json_build_health();
        if (!body) {
            send_json_response(client_fd, 500, "Internal Server Error",
                "{\"error\":{\"code\":\"INTERNAL\","
                "\"message\":\"internal error\"}}",
                origin_echo);
        } else {
            send_json_response(client_fd, 200, "OK", body, origin_echo);
            free(body);
        }
        close(client_fd);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Route: GET /api/v1/overview                                         */
    /* ------------------------------------------------------------------ */
    if (strcmp(path, "/api/v1/overview") == 0) {
        if (!is_get) {
            const char *err =
                "{\"error\":{\"code\":\"METHOD_NOT_ALLOWED\","
                "\"message\":\"use GET\"}}";
            send_json_response(client_fd, 405, "Method Not Allowed",
                               err, origin_echo);
            close(client_fd);
            return;
        }
        char *body = json_build_overview();
        if (!body) {
            send_json_response(client_fd, 500, "Internal Server Error",
                "{\"error\":{\"code\":\"INTERNAL\","
                "\"message\":\"internal error\"}}",
                origin_echo);
        } else {
            send_json_response(client_fd, 200, "OK", body, origin_echo);
            free(body);
        }
        close(client_fd);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Route: GET /api/v1/backends                                         */
    /* ------------------------------------------------------------------ */
    if (strcmp(path, "/api/v1/backends") == 0) {
        if (!is_get) {
            const char *err =
                "{\"error\":{\"code\":\"METHOD_NOT_ALLOWED\","
                "\"message\":\"use GET\"}}";
            send_json_response(client_fd, 405, "Method Not Allowed",
                               err, origin_echo);
            close(client_fd);
            return;
        }
        char *body = json_build_backends();
        if (!body) {
            send_json_response(client_fd, 500, "Internal Server Error",
                "{\"error\":{\"code\":\"INTERNAL\","
                "\"message\":\"internal error\"}}",
                origin_echo);
        } else {
            send_json_response(client_fd, 200, "OK", body, origin_echo);
            free(body);
        }
        close(client_fd);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Route: PUT /api/v1/policy                                           */
    /* ------------------------------------------------------------------ */
    if (strcmp(path, "/api/v1/policy") == 0) {
        if (!is_put) {
            const char *err =
                "{\"error\":{\"code\":\"METHOD_NOT_ALLOWED\","
                "\"message\":\"use PUT\"}}";
            send_json_response(client_fd, 405, "Method Not Allowed",
                               err, origin_echo);
            close(client_fd);
            return;
        }

        /* Read body (pass hdr_buf/hlen so already-buffered body bytes are used). */
        ssize_t blen = read_body(client_fd, &req,
                                 hdr_buf, (size_t)hlen,
                                 body_buf, sizeof(body_buf));
        if (blen == -413) {
            const char *err =
                "{\"error\":{\"code\":\"BODY_TOO_LARGE\","
                "\"message\":\"request body exceeds 4 KiB\"}}";
            send_json_response(client_fd, 413, "Payload Too Large",
                               err, origin_echo);
            close(client_fd);
            return;
        }
        if (blen < 0) {
            const char *err =
                "{\"error\":{\"code\":\"BAD_REQUEST\","
                "\"message\":\"could not read request body\"}}";
            send_json_response(client_fd, 400, "Bad Request",
                               err, origin_echo);
            close(client_fd);
            return;
        }

        /* Extract "policy" field — validate BEFORE acquiring mutex. */
        char policy_val[64];
        if (!json_extract_string(body_buf, "policy",
                                 policy_val, sizeof(policy_val))) {
            const char *err =
                "{\"error\":{\"code\":\"INVALID_POLICY\","
                "\"message\":\"missing or malformed 'policy' field\"}}";
            send_json_response(client_fd, 400, "Bad Request",
                               err, origin_echo);
            close(client_fd);
            return;
        }

        lb_policy_t new_policy;
        if (strcmp(policy_val, "round_robin") == 0) {
            new_policy = POLICY_ROUND_ROBIN;
        } else if (strcmp(policy_val, "least_connections") == 0) {
            new_policy = POLICY_LEAST_CONNECTIONS;
        } else {
            const char *err =
                "{\"error\":{\"code\":\"INVALID_POLICY\","
                "\"message\":\"policy must be round_robin or least_connections\"}}";
            send_json_response(client_fd, 400, "Bad Request",
                               err, origin_echo);
            close(client_fd);
            return;
        }

        /* Only now acquire the mutex and write. */
        pthread_mutex_lock(&g_pool.policy_mutex);
        g_pool.policy = new_policy;
        pthread_mutex_unlock(&g_pool.policy_mutex);

        log_info("admin: policy updated to %s", policy_val);

        const char *resp_val = (new_policy == POLICY_LEAST_CONNECTIONS)
                               ? "least_connections" : "round_robin";
        char resp_body[128];
        snprintf(resp_body, sizeof(resp_body),
                 "{\"policy\": \"%s\", \"updated\": true}\n",
                 resp_val);
        send_json_response(client_fd, 200, "OK", resp_body, origin_echo);
        close(client_fd);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Route: GET /api/v1/metrics[?window_seconds=N]                       */
    /* ------------------------------------------------------------------ */
    if (strcmp(path, "/api/v1/metrics") == 0) {
        if (!is_get) {
            const char *err =
                "{\"error\":{\"code\":\"METHOD_NOT_ALLOWED\","
                "\"message\":\"use GET\"}}";
            send_json_response(client_fd, 405, "Method Not Allowed",
                               err, origin_echo);
            close(client_fd);
            return;
        }

        /* Parse optional window_seconds query parameter. */
        int window_s = 60; /* default */
        const char *qp = strchr(req.target, '?');
        if (qp) {
            const char *ws_param = strstr(qp + 1, "window_seconds=");
            if (ws_param) {
                ws_param += strlen("window_seconds=");
                char *endp;
                long val = strtol(ws_param, &endp, 10);
                /* Must end at '&', '\0' or end of string, and in [10, 300]. */
                if (*endp != '\0' && *endp != '&') {
                    const char *err =
                        "{\"error\":{\"code\":\"INVALID_PARAMETER\","
                        "\"message\":\"window_seconds must be integer 10-300\"}}";
                    send_json_response(client_fd, 400, "Bad Request",
                                       err, origin_echo);
                    close(client_fd);
                    return;
                }
                if (val < 10 || val > 300) {
                    const char *err =
                        "{\"error\":{\"code\":\"INVALID_PARAMETER\","
                        "\"message\":\"window_seconds must be integer 10-300\"}}";
                    send_json_response(client_fd, 400, "Bad Request",
                                       err, origin_echo);
                    close(client_fd);
                    return;
                }
                window_s = (int)val;
            }
        }

        char *body = json_build_metrics(window_s);
        if (!body) {
            send_json_response(client_fd, 500, "Internal Server Error",
                "{\"error\":{\"code\":\"INTERNAL\","
                "\"message\":\"internal error\"}}",
                origin_echo);
        } else {
            send_json_response(client_fd, 200, "OK", body, origin_echo);
            free(body);
        }
        close(client_fd);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Route: GET /api/v1/benchmarks/latest                                */
    /* ------------------------------------------------------------------ */
    if (strcmp(path, "/api/v1/benchmarks/latest") == 0) {
        if (!is_get) {
            const char *err =
                "{\"error\":{\"code\":\"METHOD_NOT_ALLOWED\","
                "\"message\":\"use GET\"}}";
            send_json_response(client_fd, 405, "Method Not Allowed",
                               err, origin_echo);
            close(client_fd);
            return;
        }
        char *body = json_build_benchmark_latest();
        if (!body) {
            send_json_response(client_fd, 500, "Internal Server Error",
                "{\"error\":{\"code\":\"INTERNAL\","
                "\"message\":\"could not read benchmark results\"}}",
                origin_echo);
        } else {
            send_json_response(client_fd, 200, "OK", body, origin_echo);
            free(body);
        }
        close(client_fd);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Unknown route → 404                                                  */
    /* ------------------------------------------------------------------ */
    {
        const char *err =
            "{\"error\":{\"code\":\"NOT_FOUND\","
            "\"message\":\"unknown route\"}}";
        send_json_response(client_fd, 404, "Not Found", err, origin_echo);
        close(client_fd);
    }
}
