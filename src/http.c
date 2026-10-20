#include "edgemesh/http.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool token_char(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
    return strchr("!#$%&'*+-.^_`|~", (int)c) != NULL;
}

static bool has_token(const char *value, size_t length) {
    if (length == 0U) return false;
    for (size_t i = 0; i < length; ++i) {
        if (!token_char((unsigned char)value[i])) return false;
    }
    return true;
}

int em_parse_request(const unsigned char *data, size_t length, em_request *out) {
    if (data == NULL || out == NULL) return 400;
    if (length > EM_REQUEST_HEADER_MAX) return 431;
    memset(out, 0, sizeof(*out));
    const unsigned char *end = NULL;
    for (size_t i = 3U; i < length; ++i) {
        if (data[i - 3U] == '\r' && data[i - 2U] == '\n' && data[i - 1U] == '\r' && data[i] == '\n') {
            end = data + i + 1U;
            break;
        }
    }
    if (end == NULL) return 400;
    out->header_end = (size_t)(end - data);
    if (length != out->header_end) return 413;

    const unsigned char *line_end = NULL;
    for (size_t i = 1U; i < out->header_end; ++i) {
        if (data[i - 1U] == '\r' && data[i] == '\n') { line_end = data + i - 1U; break; }
    }
    if (line_end == NULL) return 400;
    size_t line_len = (size_t)(line_end - data);
    if (line_len > EM_REQUEST_LINE_MAX) return 414;
    const unsigned char *sp1 = memchr(data, ' ', line_len);
    if (sp1 == NULL || sp1 == data) return 400;
    const unsigned char *sp2 = memchr(sp1 + 1, ' ', (size_t)(line_end - sp1 - 1));
    if (sp2 == NULL || sp2 == sp1 + 1 || memchr(sp2 + 1, ' ', (size_t)(line_end - sp2 - 1)) != NULL) return 400;
    size_t method_len = (size_t)(sp1 - data);
    size_t target_len = (size_t)(sp2 - sp1 - 1);
    size_t version_len = (size_t)(line_end - sp2 - 1);
    if (method_len >= sizeof(out->method) || version_len >= sizeof(out->version) || target_len > EM_REQUEST_LINE_MAX) return 414;
    for (size_t i = 0; i < method_len; ++i) if (!token_char(data[i])) return 400;
    memcpy(out->method, data, method_len);
    memcpy(out->target, sp1 + 1, target_len);
    memcpy(out->version, sp2 + 1, version_len);

    if (strcmp(out->method, "GET") != 0) return 405;
    if (strcmp(out->version, "HTTP/1.1") != 0) return 505;
    if (out->target[0] != '/' || strstr(out->target, "#") != NULL) return 400;
    for (size_t i = 0; i < target_len; ++i) {
        unsigned char c = (unsigned char)out->target[i];
        if (c <= 0x20U || c == 0x7fU) return 400;
    }

    unsigned hosts = 0U;
    bool transfer_encoding = false;
    bool content_length_seen = false;
    bool content_length_nonzero = false;
    const unsigned char *p = line_end + 2;
    const unsigned char *limit = end - 2;
    while (p < limit) {
        const unsigned char *eol = NULL;
        for (const unsigned char *q = p; q + 1 < end; ++q) {
            if (q[0] == '\r' && q[1] == '\n') { eol = q; break; }
        }
        if (eol == NULL) return 400;
        size_t n = (size_t)(eol - p);
        if (n == 0U) break;
        if (*p == ' ' || *p == '\t') return 400;
        const unsigned char *colon = memchr(p, ':', n);
        if (colon == NULL || colon == p) return 400;
        size_t name_len = (size_t)(colon - p);
        if (!has_token((const char *)p, name_len)) return 400;
        const unsigned char *v = colon + 1;
        const unsigned char *ve = eol;
        while (v < ve && (*v == ' ' || *v == '\t')) ++v;
        while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) --ve;
        for (const unsigned char *q = v; q < ve; ++q) if ((*q < 0x20U && *q != '\t') || *q == 0x7fU) return 400;
        size_t value_len = (size_t)(ve - v);
        if (name_len == 4U && strncasecmp((const char *)p, "Host", 4U) == 0) {
            ++hosts;
            if (value_len == 0U || value_len >= sizeof(out->host)) return 400;
            memcpy(out->host, v, value_len); out->host[value_len] = '\0'; out->host_length = value_len;
        } else if (name_len == 17U && strncasecmp((const char *)p, "Transfer-Encoding", 17U) == 0) {
            transfer_encoding = true;
        } else if (name_len == 14U && strncasecmp((const char *)p, "Content-Length", 14U) == 0) {
            if (content_length_seen) return 400;
            content_length_seen = true;
            if (value_len == 0U) return 400;
            for (const unsigned char *q = v; q < ve; ++q) {
                if (!isdigit(*q)) return 400;
                if (*q != '0') content_length_nonzero = true;
            }
        }
        p = eol + 2;
    }
    if (hosts != 1U) return 400;
    for (size_t i = 0; i < out->host_length; ++i) {
        unsigned char c = (unsigned char)out->host[i];
        if (c <= 0x20U || c >= 0x7fU || c == ',' || c == '/' || c == '@' || c == '\\') return 400;
    }
    if (transfer_encoding || content_length_nonzero) return 413;
    return 0;
}
