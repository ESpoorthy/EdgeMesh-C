#include "edgemesh/proxy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_port(const char *s, unsigned short *out) {
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v == 0UL || v > 65535UL) return -1;
    *out = (unsigned short)v;
    return 0;
}

int main(int argc, char **argv) {
    unsigned short listen_port = 8080U, backend_port = 9001U;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--listen-port") == 0 && i + 1 < argc) {
            if (parse_port(argv[++i], &listen_port) != 0) { fprintf(stderr, "invalid listen port\n"); return 2; }
        } else if (strcmp(argv[i], "--backend-port") == 0 && i + 1 < argc) {
            if (parse_port(argv[++i], &backend_port) != 0) { fprintf(stderr, "invalid backend port\n"); return 2; }
        } else {
            fprintf(stderr, "usage: %s [--listen-port PORT] [--backend-port PORT]\n", argv[0]); return 2;
        }
    }
    return em_proxy_run(listen_port, backend_port) == 0 ? 0 : 1;
}
