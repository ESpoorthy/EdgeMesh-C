#include "edgemesh/http_parser.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal helper: find a byte pattern in a buffer                    */
/* (Replaces memmem which requires _GNU_SOURCE.)                       */
/* ------------------------------------------------------------------ */
static const char *mem_find(const char *haystack, size_t hlen,
                            const char *needle,   size_t nlen) {
    if (nlen == 0)        return haystack;
    if (hlen < nlen)      return NULL;
    size_t limit = hlen - nlen;
    for (size_t i = 0; i <= limit; i++) {
        if (memcmp(haystack + i, needle, nlen) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

/*
 * parse_request — parses an HTTP/1.1 GET request from buf (len bytes).
 *
 * Accepts ONLY:
 *   - method  == "GET"
 *   - target  starts with '/', length <= HTTP_MAX_TARGET_LEN-1
 *   - version == "HTTP/1.1"
 *   - headers section <= HTTP_MAX_REQUEST_SIZE bytes total
 *   - at most HTTP_MAX_HEADERS name/value pairs
 *
 * Returns a parse_result_t value; fills *out on PARSE_OK.
 */
parse_result_t parse_request(const char *buf, size_t len,
                             http_request_t *out) {
    if (buf == NULL || len == 0 || out == NULL) {
        return PARSE_BAD_REQUEST;
    }

    /* Reject oversized header sections before touching the buffer. */
    if (len > HTTP_MAX_REQUEST_SIZE) {
        return PARSE_HEADER_TOO_LARGE;
    }

    /* Work on a local pointer; do not modify caller's buffer. */
    const char *p   = buf;
    const char *end = buf + len;

    /* ------------------------------------------------------------------ */
    /* 1. Request line: METHOD SP target SP HTTP/1.1 CRLF                  */
    /* ------------------------------------------------------------------ */
    const char *line_end = mem_find(p, (size_t)(end - p), "\r\n", 2);
    if (line_end == NULL) {
        return PARSE_BAD_REQUEST;
    }

    /* Parse method. */
    const char *sp1 = (const char *)memchr(p, ' ', (size_t)(line_end - p));
    if (sp1 == NULL) {
        return PARSE_BAD_REQUEST;
    }
    size_t method_len = (size_t)(sp1 - p);
    if (method_len == 0 || method_len >= HTTP_MAX_METHOD_LEN) {
        return PARSE_BAD_REQUEST;
    }

    char method_tmp[HTTP_MAX_METHOD_LEN];
    memcpy(method_tmp, p, method_len);
    method_tmp[method_len] = '\0';

    /* Only GET is supported. */
    if (strcmp(method_tmp, "GET") != 0) {
        return PARSE_METHOD_NOT_ALLOWED;
    }

    /* Parse request target. */
    const char *target_start = sp1 + 1;
    if (target_start >= line_end) {
        return PARSE_BAD_REQUEST;
    }
    const char *sp2 = (const char *)memchr(target_start, ' ',
                                           (size_t)(line_end - target_start));
    if (sp2 == NULL) {
        return PARSE_BAD_REQUEST;
    }
    size_t target_len = (size_t)(sp2 - target_start);
    if (target_len == 0) {
        return PARSE_BAD_REQUEST;
    }

    /* Target must start with '/'. */
    if (target_start[0] != '/') {
        return PARSE_BAD_REQUEST;
    }

    /* Target length limit (leave room for NUL). */
    if (target_len >= HTTP_MAX_TARGET_LEN) {
        return PARSE_URI_TOO_LONG;
    }

    /* Parse HTTP version. */
    const char *version_start = sp2 + 1;
    size_t version_len = (size_t)(line_end - version_start);
    if (version_len == 0 || version_len >= HTTP_MAX_VERSION_LEN) {
        return PARSE_BAD_REQUEST;
    }

    char version_tmp[HTTP_MAX_VERSION_LEN];
    memcpy(version_tmp, version_start, version_len);
    version_tmp[version_len] = '\0';

    if (strcmp(version_tmp, "HTTP/1.1") != 0) {
        return PARSE_BAD_REQUEST;
    }

    /* ------------------------------------------------------------------ */
    /* 2. Header fields                                                     */
    /* ------------------------------------------------------------------ */
    const char *headers_start = line_end + 2; /* skip first CRLF */
    int hcount = 0;
    http_header_t hdrs[HTTP_MAX_HEADERS];

    p = headers_start;

    int found_end = 0; /* set when we see the blank-line terminator */

    while (p < end) {
        /* Empty line (CRLF) signals end of headers. */
        if (end - p >= 2 && p[0] == '\r' && p[1] == '\n') {
            found_end = 1;
            break; /* end-of-headers */
        }

        const char *hdr_end = mem_find(p, (size_t)(end - p), "\r\n", 2);
        if (hdr_end == NULL) {
            return PARSE_BAD_REQUEST; /* incomplete header */
        }

        /* Split on ':' */
        const char *colon = (const char *)memchr(p, ':',
                                                 (size_t)(hdr_end - p));
        if (colon == NULL) {
            return PARSE_BAD_REQUEST;
        }

        size_t name_len = (size_t)(colon - p);
        if (name_len == 0 || name_len >= HTTP_MAX_HEADER_NAME) {
            return PARSE_BAD_REQUEST;
        }

        /* Value: skip leading whitespace after ':' */
        const char *val_start = colon + 1;
        while (val_start < hdr_end &&
               (*val_start == ' ' || *val_start == '\t')) {
            val_start++;
        }
        size_t val_len = (size_t)(hdr_end - val_start);
        if (val_len >= HTTP_MAX_HEADER_VALUE) {
            return PARSE_BAD_REQUEST;
        }

        if (hcount >= HTTP_MAX_HEADERS) {
            return PARSE_BAD_REQUEST; /* too many headers */
        }

        memcpy(hdrs[hcount].name,  p,         name_len);
        hdrs[hcount].name[name_len] = '\0';
        memcpy(hdrs[hcount].value, val_start, val_len);
        hdrs[hcount].value[val_len] = '\0';
        hcount++;

        p = hdr_end + 2; /* advance past CRLF */
    }

    /* Require the blank-line terminator. */
    if (!found_end) {
        return PARSE_BAD_REQUEST;
    }

    /* ------------------------------------------------------------------ */
    /* 3. Populate output                                                    */
    /* ------------------------------------------------------------------ */
    memset(out, 0, sizeof(*out));
    memcpy(out->method,  method_tmp,   method_len  + 1);
    memcpy(out->target,  target_start, target_len);
    out->target[target_len] = '\0';
    memcpy(out->version, version_tmp,  version_len + 1);
    out->header_count = hcount;
    memcpy(out->headers, hdrs, (size_t)hcount * sizeof(http_header_t));

    return PARSE_OK;
}
