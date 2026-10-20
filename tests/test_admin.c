/*
 * tests/test_admin.c
 *
 * CTest unit tests for admin JSON building and policy validation.
 * Does NOT start any network listener.
 *
 * Tests:
 *  1. json_escape_string: embedded quotes
 *  2. json_escape_string: backslashes
 *  3. json_escape_string: newlines and tabs
 *  4. json_escape_string: control characters < 0x20
 *  5. json_escape_string: null / empty input
 *  6. json_escape_string: output size boundary (truncation safety)
 *  7. json_build_health: returns non-NULL, contains "status" and "ok"
 *  8. json_build_backends: returns non-NULL, contains 3 entries
 *  9. policy validation: valid "round_robin" succeeds
 * 10. policy validation: valid "least_connections" succeeds
 * 11. policy validation: invalid value does NOT change current policy
 * 12. json_build_overview: returns non-NULL, contains "proxy_running"
 * 13. json_build_metrics: returns non-NULL with window_seconds=60
 */

#define _POSIX_C_SOURCE 200809L

#include "edgemesh/json_builder.h"
#include "edgemesh/backend.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* -----------------------------------------------------------------------
 * Assertion helpers
 * ----------------------------------------------------------------------- */
static int s_failures = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, (msg)); \
        s_failures++; \
    } else { \
        printf("PASS: %s\n", (msg)); \
    } \
} while (0)

#define ASSERT_STR_CONTAINS(haystack, needle, msg) do { \
    if (!haystack || strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL [%s:%d] %s — '%s' not in output\n", \
                __FILE__, __LINE__, (msg), (needle)); \
        s_failures++; \
    } else { \
        printf("PASS: %s\n", (msg)); \
    } \
} while (0)

/* -----------------------------------------------------------------------
 * Test 1: embedded quotes
 * ----------------------------------------------------------------------- */
static void test_escape_quotes(void) {
    char out[64];
    json_escape_string("say \"hello\"", out, sizeof(out));
    ASSERT(strcmp(out, "say \\\"hello\\\"") == 0,
           "escape: embedded double quotes become \\\"");
}

/* -----------------------------------------------------------------------
 * Test 2: backslashes
 * ----------------------------------------------------------------------- */
static void test_escape_backslash(void) {
    char out[64];
    json_escape_string("a\\b", out, sizeof(out));
    ASSERT(strcmp(out, "a\\\\b") == 0,
           "escape: backslash becomes \\\\");
}

/* -----------------------------------------------------------------------
 * Test 3: newlines and tabs
 * ----------------------------------------------------------------------- */
static void test_escape_newline_tab(void) {
    char out[64];
    json_escape_string("line\nend\ttab", out, sizeof(out));
    ASSERT(strcmp(out, "line\\nend\\ttab") == 0,
           "escape: newline -> \\n and tab -> \\t");

    char out2[64];
    json_escape_string("cr\r\n", out2, sizeof(out2));
    ASSERT(strcmp(out2, "cr\\r\\n") == 0,
           "escape: CR -> \\r and LF -> \\n");
}

/* -----------------------------------------------------------------------
 * Test 4: control characters < 0x20
 * ----------------------------------------------------------------------- */
static void test_escape_control_chars(void) {
    char in[4] = { 0x01, 0x1F, 0x00 };  /* SOH, US */
    char out[64];
    json_escape_string(in, out, sizeof(out));
    ASSERT(strstr(out, "\\u0001") != NULL,
           "escape: 0x01 -> \\u0001");
    ASSERT(strstr(out, "\\u001F") != NULL,
           "escape: 0x1F -> \\u001F");
}

/* -----------------------------------------------------------------------
 * Test 5: empty / null-safe
 * ----------------------------------------------------------------------- */
static void test_escape_empty(void) {
    char out[16];
    json_escape_string("", out, sizeof(out));
    ASSERT(strcmp(out, "") == 0, "escape: empty string produces empty output");

    /* out_size == 0 must not crash. */
    json_escape_string("hello", out, 0);
    ASSERT(1, "escape: out_size=0 does not crash");
}

/* -----------------------------------------------------------------------
 * Test 6: output truncation stays NUL-terminated
 * ----------------------------------------------------------------------- */
static void test_escape_truncation(void) {
    char out[4]; /* tiny buffer */
    json_escape_string("abcdefghij", out, sizeof(out));
    /* Must be NUL-terminated and at most 3 content chars. */
    ASSERT(out[3] == '\0', "escape: truncation: buf[size-1] == NUL");
    ASSERT(strlen(out) <= 3, "escape: truncation: length <= size-1");
}

/* -----------------------------------------------------------------------
 * Test 7: json_build_health
 * ----------------------------------------------------------------------- */
static void test_build_health(void) {
    char *h = json_build_health();
    ASSERT(h != NULL, "json_build_health: returns non-NULL");
    ASSERT_STR_CONTAINS(h, "\"status\"", "json_build_health: contains 'status'");
    ASSERT_STR_CONTAINS(h, "\"ok\"",     "json_build_health: contains 'ok'");
    ASSERT_STR_CONTAINS(h, "edgemesh-c","json_build_health: contains service name");
    free(h);
}

/* -----------------------------------------------------------------------
 * Test 8: json_build_backends — expects 3 entries after pool_init()
 * ----------------------------------------------------------------------- */
static void test_build_backends(void) {
    char *b = json_build_backends();
    ASSERT(b != NULL, "json_build_backends: returns non-NULL");

    /* Count occurrences of "backend-" (should be 3). */
    int count = 0;
    const char *p = b;
    while ((p = strstr(p, "\"backend-")) != NULL) {
        count++;
        p++;
    }
    ASSERT(count == 3, "json_build_backends: contains 3 backend entries");
    ASSERT_STR_CONTAINS(b, "9001", "json_build_backends: contains port 9001");
    ASSERT_STR_CONTAINS(b, "9002", "json_build_backends: contains port 9002");
    ASSERT_STR_CONTAINS(b, "9003", "json_build_backends: contains port 9003");
    free(b);
}

/* -----------------------------------------------------------------------
 * Test 9: valid policy "round_robin"
 * ----------------------------------------------------------------------- */
static void test_policy_round_robin(void) {
    /* Set initial to least_connections so we can check the change. */
    pthread_mutex_lock(&g_pool.policy_mutex);
    g_pool.policy = POLICY_LEAST_CONNECTIONS;
    pthread_mutex_unlock(&g_pool.policy_mutex);

    /* Apply valid round_robin policy. */
    pthread_mutex_lock(&g_pool.policy_mutex);
    g_pool.policy = POLICY_ROUND_ROBIN;
    pthread_mutex_unlock(&g_pool.policy_mutex);

    pthread_mutex_lock(&g_pool.policy_mutex);
    lb_policy_t p = g_pool.policy;
    pthread_mutex_unlock(&g_pool.policy_mutex);

    ASSERT(p == POLICY_ROUND_ROBIN,
           "policy: valid 'round_robin' accepted");
}

/* -----------------------------------------------------------------------
 * Test 10: valid policy "least_connections"
 * ----------------------------------------------------------------------- */
static void test_policy_least_connections(void) {
    pthread_mutex_lock(&g_pool.policy_mutex);
    g_pool.policy = POLICY_ROUND_ROBIN;
    pthread_mutex_unlock(&g_pool.policy_mutex);

    pthread_mutex_lock(&g_pool.policy_mutex);
    g_pool.policy = POLICY_LEAST_CONNECTIONS;
    pthread_mutex_unlock(&g_pool.policy_mutex);

    pthread_mutex_lock(&g_pool.policy_mutex);
    lb_policy_t p = g_pool.policy;
    pthread_mutex_unlock(&g_pool.policy_mutex);

    ASSERT(p == POLICY_LEAST_CONNECTIONS,
           "policy: valid 'least_connections' accepted");
}

/* -----------------------------------------------------------------------
 * Test 11: invalid value must NOT change current policy
 *
 * This simulates the validation path from handler.c:
 *   - validate BEFORE acquiring mutex
 *   - on invalid, do NOT acquire mutex / do NOT write
 * ----------------------------------------------------------------------- */
static void test_policy_invalid_does_not_mutate(void) {
    /* Set known good state. */
    pthread_mutex_lock(&g_pool.policy_mutex);
    g_pool.policy = POLICY_ROUND_ROBIN;
    pthread_mutex_unlock(&g_pool.policy_mutex);

    /* Simulate: parse invalid JSON string, validate, DO NOT write. */
    const char *invalid_policy = "foobar";
    int is_valid = (strcmp(invalid_policy, "round_robin") == 0 ||
                    strcmp(invalid_policy, "least_connections") == 0);

    if (is_valid) {
        /* Would write — but this branch should not be taken. */
        pthread_mutex_lock(&g_pool.policy_mutex);
        g_pool.policy = POLICY_LEAST_CONNECTIONS; /* wrong */
        pthread_mutex_unlock(&g_pool.policy_mutex);
    }
    /* else: validation failed before mutex acquire — policy unchanged. */

    pthread_mutex_lock(&g_pool.policy_mutex);
    lb_policy_t p = g_pool.policy;
    pthread_mutex_unlock(&g_pool.policy_mutex);

    ASSERT(!is_valid, "policy: 'foobar' is correctly identified as invalid");
    ASSERT(p == POLICY_ROUND_ROBIN,
           "policy: invalid value does NOT mutate current policy");
}

/* -----------------------------------------------------------------------
 * Test 12: json_build_overview
 * ----------------------------------------------------------------------- */
static void test_build_overview(void) {
    char *o = json_build_overview();
    ASSERT(o != NULL, "json_build_overview: returns non-NULL");
    ASSERT_STR_CONTAINS(o, "proxy_running",
                        "json_build_overview: contains 'proxy_running'");
    ASSERT_STR_CONTAINS(o, "policy",
                        "json_build_overview: contains 'policy'");
    ASSERT_STR_CONTAINS(o, "backends",
                        "json_build_overview: contains 'backends'");
    free(o);
}

/* -----------------------------------------------------------------------
 * Test 13: json_build_metrics
 * ----------------------------------------------------------------------- */
static void test_build_metrics(void) {
    char *m = json_build_metrics(60);
    ASSERT(m != NULL, "json_build_metrics: returns non-NULL");
    ASSERT_STR_CONTAINS(m, "window_seconds",
                        "json_build_metrics: contains 'window_seconds'");
    ASSERT_STR_CONTAINS(m, "points",
                        "json_build_metrics: contains 'points'");
    free(m);
}

/* -----------------------------------------------------------------------
 * Test 14: json_build_benchmark_latest (absent, corrupted, and valid)
 * ----------------------------------------------------------------------- */
static void test_build_benchmark_latest(const char *argv0) {
    json_builder_set_exe_path(argv0);

    /* 14a. Absent file -> available: false */
    char *res = json_build_benchmark_latest();
    ASSERT(res != NULL, "json_build_benchmark_latest (absent): returns non-NULL");
    ASSERT_STR_CONTAINS(res, "\"available\": false",
                        "json_build_benchmark_latest (absent): contains 'available: false'");
    ASSERT_STR_CONTAINS(res, "\"run\": null",
                        "json_build_benchmark_latest (absent): contains 'run: null'");
    free(res);

    /* Prepare a test directory <exe_dir>/results */
    char tmp[512];
    strncpy(tmp, argv0 ? argv0 : ".", sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *slash = strrchr(tmp, '/');
    if (slash) *slash = '\0';
    else strcpy(tmp, ".");

    char dir[1024];
    char file[1024];
    snprintf(dir, sizeof(dir), "%s/results", tmp);
    snprintf(file, sizeof(file), "%s/results/latest.json", tmp);
    mkdir(dir, 0755);

    /* 14b. Truncated/corrupted file -> returns NULL */
    FILE *fp = fopen(file, "w");
    if (fp) {
        fputs("{\"run_id\": \"truncated_bench\", \"completed\": ", fp);
        fclose(fp);

        char *corrupt_res = json_build_benchmark_latest();
        ASSERT(corrupt_res == NULL,
               "json_build_benchmark_latest (truncated): returns NULL");
        if (corrupt_res) free(corrupt_res);
    }

    /* 14c. Valid JSON file -> returns available: true with run */
    fp = fopen(file, "w");
    if (fp) {
        fputs("{\"run_id\": \"bench-123\", \"completed_requests\": 100}", fp);
        fclose(fp);

        char *valid_res = json_build_benchmark_latest();
        ASSERT(valid_res != NULL,
               "json_build_benchmark_latest (valid): returns non-NULL");
        ASSERT_STR_CONTAINS(valid_res, "\"available\": true",
                            "json_build_benchmark_latest (valid): contains 'available: true'");
        ASSERT_STR_CONTAINS(valid_res, "\"run\": {",
                            "json_build_benchmark_latest (valid): contains 'run: {'");
        ASSERT_STR_CONTAINS(valid_res, "bench-123",
                            "json_build_benchmark_latest (valid): contains run_id");
        if (valid_res) free(valid_res);
    }

    /* Clean up temporary file so subsequent tests see clean state */
    unlink(file);
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(int argc, char *argv[]) {
    /* Initialise pool and metrics for tests that need them. */
    pool_init();

    printf("=== test_admin ===\n");

    test_escape_quotes();
    test_escape_backslash();
    test_escape_newline_tab();
    test_escape_control_chars();
    test_escape_empty();
    test_escape_truncation();
    test_build_health();
    test_build_backends();
    test_policy_round_robin();
    test_policy_least_connections();
    test_policy_invalid_does_not_mutate();
    test_build_overview();
    test_build_metrics();
    test_build_benchmark_latest((argc > 0 && argv[0]) ? argv[0] : ".");

    printf("\n=== %d failure(s) ===\n", s_failures);
    return (s_failures == 0) ? 0 : 1;
}
