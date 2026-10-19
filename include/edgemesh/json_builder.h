#ifndef EDGEMESH_JSON_BUILDER_H
#define EDGEMESH_JSON_BUILDER_H

#include <stddef.h>

/*
 * json_builder_set_exe_path(path)
 *
 * Must be called once from main() with argv[0] so that
 * json_build_benchmark_latest() can resolve results/latest.json
 * relative to the executable's own directory (not getcwd()).
 */
void json_builder_set_exe_path(const char *path);

/*
 * json_escape_string(in, out, out_size)
 *
 * Copies in into out, escaping characters that are not safe in a JSON string:
 *   "  → \"
 *   \  → \\
 *   \r → \r  (literal two chars)
 *   \n → \n  (literal two chars)
 *   \t → \t  (literal two chars)
 *   control chars < 0x20 → \uXXXX
 * out is NUL-terminated; at most out_size-1 escaped characters are written.
 */
void json_escape_string(const char *in, char *out, size_t out_size);

/*
 * json_build_health()
 * json_build_overview()
 * json_build_backends()
 * json_build_metrics(window_s)
 * json_build_benchmark_latest()
 *
 * Each returns a heap-allocated, NUL-terminated JSON string.
 * The caller is responsible for free()-ing the returned pointer.
 * Returns NULL on allocation failure.
 * benchmark_latest reads ONLY results/latest.json resolved from the
 * executable's own directory — never from a caller-supplied path.
 */
char *json_build_health(void);
char *json_build_overview(void);
char *json_build_backends(void);
char *json_build_metrics(int window_s);
char *json_build_benchmark_latest(void);

#endif /* EDGEMESH_JSON_BUILDER_H */
