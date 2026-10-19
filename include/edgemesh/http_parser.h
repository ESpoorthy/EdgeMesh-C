#ifndef EDGEMESH_HTTP_PARSER_H
#define EDGEMESH_HTTP_PARSER_H

#include <stddef.h>

/* Maximum sizes — must match requirements.md §3 FR-01. */
#define HTTP_MAX_METHOD_LEN   16
#define HTTP_MAX_TARGET_LEN   2048
#define HTTP_MAX_VERSION_LEN  16
#define HTTP_MAX_HEADERS      32
#define HTTP_MAX_HEADER_NAME  128
#define HTTP_MAX_HEADER_VALUE 1024
#define HTTP_MAX_REQUEST_SIZE 16384  /* 16 KiB */

/* Result codes for parse_request(). */
typedef enum {
    PARSE_OK                 = 0,
    PARSE_BAD_REQUEST        = 1,  /* malformed request line or headers → 400 */
    PARSE_METHOD_NOT_ALLOWED = 2,  /* non-GET method → 405 */
    PARSE_URI_TOO_LONG       = 3,  /* target > 2047 chars → 414 */
    PARSE_HEADER_TOO_LARGE   = 4   /* header section > 16 KiB → 431 */
} parse_result_t;

/* A single header name/value pair. */
typedef struct {
    char name[HTTP_MAX_HEADER_NAME];
    char value[HTTP_MAX_HEADER_VALUE];
} http_header_t;

/* Parsed HTTP request (GET only, origin-form target). */
typedef struct {
    char          method[HTTP_MAX_METHOD_LEN];
    char          target[HTTP_MAX_TARGET_LEN];
    char          version[HTTP_MAX_VERSION_LEN];
    http_header_t headers[HTTP_MAX_HEADERS];
    int           header_count;
} http_request_t;

/*
 * parse_request(buf, len, out)
 *
 * Parses an HTTP/1.1 GET request from buf (len bytes) into *out.
 * Accepts only: method == "GET", target starts with '/', version == "HTTP/1.1".
 * Returns a parse_result_t value.
 */
parse_result_t parse_request(const char *buf, size_t len, http_request_t *out);

#endif /* EDGEMESH_HTTP_PARSER_H */
