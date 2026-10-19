/* admin_api/json_builder.c — stub for FEAT-001; implemented in FEAT-002 */
#include "edgemesh/json_builder.h"
#include <string.h>

void json_escape_string(const char *in, char *out, size_t out_size) {
    if (out_size > 0) {
        strncpy(out, in, out_size - 1);
        out[out_size - 1] = '\0';
    }
}

char *json_build_health(void)            { return NULL; }
char *json_build_overview(void)          { return NULL; }
char *json_build_backends(void)          { return NULL; }
char *json_build_metrics(int window_s)   { (void)window_s; return NULL; }
char *json_build_benchmark_latest(void)  { return NULL; }
