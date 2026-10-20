#ifndef EDGEMESH_HTTP_H
#define EDGEMESH_HTTP_H

#include <stddef.h>

#define EM_REQUEST_HEADER_MAX 16384U
#define EM_REQUEST_LINE_MAX 8192U
#define EM_RESPONSE_HEADER_MAX 16384U
#define EM_RESPONSE_BODY_MAX (8U * 1024U * 1024U)

typedef struct {
    char method[16];
    char target[EM_REQUEST_LINE_MAX + 1U];
    char version[16];
    char host[1024];
    size_t host_length;
    size_t header_end;
} em_request;

/* Returns 0 on success, otherwise the required proxy HTTP status. */
int em_parse_request(const unsigned char *data, size_t length, em_request *out);

#endif
