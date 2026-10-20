#include "edgemesh/http.h"
#include <stdio.h>
#include <string.h>

static int check(const char *name, const char *request, int expected) {
    em_request parsed;
    int actual = em_parse_request((const unsigned char *)request, strlen(request), &parsed);
    if (actual != expected) { fprintf(stderr, "%s: expected %d got %d\n", name, expected, actual); return 1; }
    return 0;
}

int main(void) {
    int failed = 0;
    const char *valid = "GET /hello?q=1 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    failed += check("valid GET", valid, 0);
    failed += check("malformed", "GET / HTTP/1.1\r\nHost localhost\r\n\r\n", 400);
    failed += check("missing Host", "GET / HTTP/1.1\r\n\r\n", 400);
    failed += check("unsupported method", "POST / HTTP/1.1\r\nHost: localhost\r\n\r\n", 405);
    failed += check("unsupported version", "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n", 505);
    failed += check("body framing", "GET / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1\r\n\r\n", 413);
    failed += check("absolute form", "GET http://example.test/ HTTP/1.1\r\nHost: localhost\r\n\r\n", 400);
    char big[EM_REQUEST_LINE_MAX + 128U];
    size_t prefix = (size_t)snprintf(big, sizeof(big), "GET /");
    memset(big + prefix, 'a', EM_REQUEST_LINE_MAX);
    memcpy(big + prefix + EM_REQUEST_LINE_MAX, " HTTP/1.1\r\nHost: x\r\n\r\n", 24U);
    failed += check("oversized line", big, 414);
    return failed == 0 ? 0 : 1;
}
