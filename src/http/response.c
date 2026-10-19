#include "edgemesh/http_response.h"
#include "edgemesh/io_utils.h"

#include <stdio.h>
#include <string.h>

/*
 * send_response — builds and sends an HTTP/1.1 response.
 *
 * Always includes:
 *   Connection: close
 *   Content-Length: <body_len>
 * Plus any caller-supplied headers (e.g. Content-Type).
 */
void send_response(int fd, int status_code, const char *status_text,
                   const http_header_t *headers, int header_count,
                   const char *body, size_t body_len) {
    /* Build the response header into a stack buffer. */
    char hdr_buf[4096];
    int  pos = 0;
    int  rem = (int)sizeof(hdr_buf);

    /* Status line. */
    int n = snprintf(hdr_buf + pos, (size_t)rem,
                     "HTTP/1.1 %d %s\r\n", status_code, status_text);
    if (n < 0 || n >= rem) goto truncated;
    pos += n; rem -= n;

    /* Mandatory headers. */
    n = snprintf(hdr_buf + pos, (size_t)rem,
                 "Connection: close\r\nContent-Length: %zu\r\n",
                 body_len);
    if (n < 0 || n >= rem) goto truncated;
    pos += n; rem -= n;

    /* Caller-supplied headers. */
    for (int i = 0; i < header_count && i < HTTP_MAX_HEADERS; i++) {
        n = snprintf(hdr_buf + pos, (size_t)rem,
                     "%s: %s\r\n", headers[i].name, headers[i].value);
        if (n < 0 || n >= rem) goto truncated;
        pos += n; rem -= n;
    }

    /* End of headers. */
    n = snprintf(hdr_buf + pos, (size_t)rem, "\r\n");
    if (n < 0 || n >= rem) goto truncated;
    pos += n;

    write_all(fd, hdr_buf, (size_t)pos);

    if (body != NULL && body_len > 0) {
        write_all(fd, body, body_len);
    }
    return;

truncated:
    /* If the header buffer overflowed, send a minimal 500. */
    {
        const char minimal[] =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Connection: close\r\nContent-Length: 0\r\n\r\n";
        write_all(fd, minimal, sizeof(minimal) - 1);
    }
}

/*
 * send_error_response — convenience wrapper that sends a JSON error body.
 */
void send_error_response(int fd, int status_code, const char *json_body) {
    http_header_t ct_header;
    strncpy(ct_header.name,  "Content-Type",     sizeof(ct_header.name)  - 1);
    ct_header.name[sizeof(ct_header.name) - 1] = '\0';
    strncpy(ct_header.value, "application/json", sizeof(ct_header.value) - 1);
    ct_header.value[sizeof(ct_header.value) - 1] = '\0';

    /* Derive a minimal status text. */
    const char *text;
    switch (status_code) {
        case 400: text = "Bad Request";           break;
        case 404: text = "Not Found";             break;
        case 405: text = "Method Not Allowed";    break;
        case 413: text = "Request Entity Too Large"; break;
        case 414: text = "URI Too Long";          break;
        case 431: text = "Request Header Fields Too Large"; break;
        case 500: text = "Internal Server Error"; break;
        case 502: text = "Bad Gateway";           break;
        case 503: text = "Service Unavailable";   break;
        case 504: text = "Gateway Timeout";       break;
        default:  text = "Error";                 break;
    }

    size_t body_len = json_body ? strlen(json_body) : 0;
    send_response(fd, status_code, text, &ct_header, 1,
                  json_body, body_len);
}
