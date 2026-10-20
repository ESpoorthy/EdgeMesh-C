#include "edgemesh/proxy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_port(const char *text, unsigned short *out) {
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0UL || value > 65535UL) return -1;
    *out = (unsigned short)value;
    return 0;
}

int main(int argc, char **argv) {
    unsigned short listen_port = 8080U;
    unsigned short backend_base_port = 9001U;
    em_policy policy = EM_POLICY_ROUND_ROBIN;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--listen-port") == 0 && i + 1 < argc) {
            if (parse_port(argv[++i], &listen_port) != 0) {
                fprintf(stderr, "invalid listen port\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--backend-port") == 0 && i + 1 < argc) {
            if (parse_port(argv[++i], &backend_base_port) != 0 ||
                (unsigned)backend_base_port + 2U > 65535U) {
                fprintf(stderr, "backend base port must allow three consecutive ports\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "round_robin") == 0) policy = EM_POLICY_ROUND_ROBIN;
            else if (strcmp(value, "least_connections") == 0) policy = EM_POLICY_LEAST_CONNECTIONS;
            else {
                fprintf(stderr, "policy must be round_robin or least_connections\n");
                return 2;
            }
        } else {
            fprintf(stderr, "usage: %s [--listen-port PORT] [--backend-port BASE_PORT] [--policy round_robin|least_connections]\n", argv[0]);
            return 2;
        }
    }

    return em_proxy_run(listen_port, backend_base_port, policy) == 0 ? 0 : 1;
}
