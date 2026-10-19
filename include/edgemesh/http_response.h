#ifndef EDGEMESH_HTTP_RESPONSE_H
#define EDGEMESH_HTTP_RESPONSE_H

#include <stddef.h>
#include "edgemesh/http_parser.h"  /* for http_header_t */

/*
 * send_response(fd, status_code, status_text, headers, header_count, body, body_len)
 *
 * Builds and sends an HTTP/1.1 response.  Always includes:
 *   Connection: close
 *   Content-Length: <body_len>
 * Plus any caller-supplied headers.  Uses write_all() for sending.
 */
void send_response(int fd, int status_code, const char *status_text,
                   const http_header_t *headers, int header_count,
                   const char *body, size_t body_len);

/*
 * send_error_response(fd, status_code, json_body)
 *
 * Sends a JSON error response with Content-Type: application/json.
 * json_body must be a valid, pre-escaped JSON string.
 */
void send_error_response(int fd, int status_code, const char *json_body);

#endif /* EDGEMESH_HTTP_RESPONSE_H */
