#include "edgemesh/http_parser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */
static void check(const char *label, parse_result_t got, parse_result_t want) {
    if (got != want) {
        fprintf(stderr, "FAIL [%s]: got %d, expected %d\n", label, got, want);
    } else {
        printf("PASS [%s]\n", label);
    }
    assert(got == want);
}

/* ------------------------------------------------------------------ */
/* Tests                                                                */
/* ------------------------------------------------------------------ */

/* 1. Valid GET / */
static void test_valid_root(void) {
    const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    http_request_t r;
    parse_result_t rc = parse_request(req, strlen(req), &r);
    check("valid_root", rc, PARSE_OK);
    assert(strcmp(r.method, "GET") == 0);
    assert(strcmp(r.target, "/") == 0);
    assert(strcmp(r.version, "HTTP/1.1") == 0);
}

/* 2. Valid GET with query string */
static void test_valid_query(void) {
    const char *req = "GET /search?q=hello&page=2 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    http_request_t r;
    parse_result_t rc = parse_request(req, strlen(req), &r);
    check("valid_query", rc, PARSE_OK);
    assert(strncmp(r.target, "/search", 7) == 0);
}

/* 3. GET with 10 valid headers */
static void test_ten_headers(void) {
    const char *req =
        "GET /ten HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Accept: */*\r\n"
        "Accept-Language: en\r\n"
        "Accept-Encoding: gzip\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "User-Agent: test\r\n"
        "X-Custom-1: val1\r\n"
        "X-Custom-2: val2\r\n"
        "X-Custom-3: val3\r\n"
        "\r\n";
    http_request_t r;
    parse_result_t rc = parse_request(req, strlen(req), &r);
    check("ten_headers", rc, PARSE_OK);
    assert(r.header_count == 10);
}

/* 4. POST → PARSE_METHOD_NOT_ALLOWED */
static void test_post_not_allowed(void) {
    const char *req = "POST /api HTTP/1.1\r\nHost: localhost\r\n\r\n";
    http_request_t r;
    parse_result_t rc = parse_request(req, strlen(req), &r);
    check("post_not_allowed", rc, PARSE_METHOD_NOT_ALLOWED);
}

/* 5. Missing Host header — still PARSE_OK (Host validation is proxy's job) */
static void test_no_host_ok(void) {
    const char *req = "GET /path HTTP/1.1\r\nAccept: */*\r\n\r\n";
    http_request_t r;
    parse_result_t rc = parse_request(req, strlen(req), &r);
    check("no_host_ok", rc, PARSE_OK);
}

/* 6. Malformed request line → PARSE_BAD_REQUEST */
static void test_malformed_line(void) {
    const char *req = "BADLINE\r\n\r\n";
    http_request_t r;
    parse_result_t rc = parse_request(req, strlen(req), &r);
    check("malformed_line", rc, PARSE_BAD_REQUEST);
}

/* 7. Oversized headers (>16 KiB) → PARSE_HEADER_TOO_LARGE */
static void test_oversized_headers(void) {
    /* Build a request that is larger than HTTP_MAX_REQUEST_SIZE (16384 bytes). */
    static char big[HTTP_MAX_REQUEST_SIZE + 256];
    int pos = snprintf(big, sizeof(big), "GET / HTTP/1.1\r\nHost: localhost\r\n");
    /* Pad with a long header value. */
    int i;
    for (i = pos; i < HTTP_MAX_REQUEST_SIZE + 100 && i < (int)sizeof(big) - 4; i++) {
        big[i] = 'A';
    }
    /* Don't bother with proper CRLF — the length check fires first. */
    parse_result_t rc = parse_request(big, (size_t)(HTTP_MAX_REQUEST_SIZE + 100),
                                      &(http_request_t){0});
    check("oversized_headers", rc, PARSE_HEADER_TOO_LARGE);
}

/* 8. Target > 2047 chars → PARSE_URI_TOO_LONG */
static void test_uri_too_long(void) {
    static char req[4096];
    /* Construct: GET /AAAA...A(2048) HTTP/1.1\r\n\r\n */
    int pos = 0;
    req[pos++] = 'G'; req[pos++] = 'E'; req[pos++] = 'T'; req[pos++] = ' ';
    req[pos++] = '/';
    for (int i = 0; i < HTTP_MAX_TARGET_LEN; i++) { /* 2048 chars after '/' */
        req[pos++] = 'a';
    }
    req[pos++] = ' ';
    const char *ver = "HTTP/1.1\r\nHost: x\r\n\r\n";
    memcpy(req + pos, ver, strlen(ver));
    pos += (int)strlen(ver);

    http_request_t r;
    parse_result_t rc = parse_request(req, (size_t)pos, &r);
    check("uri_too_long", rc, PARSE_URI_TOO_LONG);
}

/* 9. Incomplete headers (no \r\n\r\n) → PARSE_BAD_REQUEST */
static void test_incomplete(void) {
    const char *req = "GET / HTTP/1.1\r\nHost: localhost\r\n"; /* no final CRLF */
    http_request_t r;
    parse_result_t rc = parse_request(req, strlen(req), &r);
    check("incomplete", rc, PARSE_BAD_REQUEST);
}

/* 10. Empty input → PARSE_BAD_REQUEST */
static void test_empty(void) {
    http_request_t r;
    parse_result_t rc = parse_request("", 0, &r);
    check("empty_input", rc, PARSE_BAD_REQUEST);
}

/* ------------------------------------------------------------------ */
int main(void) {
    test_valid_root();
    test_valid_query();
    test_ten_headers();
    test_post_not_allowed();
    test_no_host_ok();
    test_malformed_line();
    test_oversized_headers();
    test_uri_too_long();
    test_incomplete();
    test_empty();

    printf("test_http_parser: all tests passed\n");
    return 0;
}
