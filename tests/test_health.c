#include "edgemesh/backend.h"
#include "edgemesh/health.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const char *label, int cond) {
    if (!cond) {
        fprintf(stderr, "FAIL [%s]\n", label);
    } else {
        printf("PASS [%s]\n", label);
    }
    assert(cond);
}

/*
 * Simulate the state-machine logic that checker.c applies on each probe
 * result, WITHOUT doing any actual network I/O.
 */
static void apply_probe(backend_t *b, int success) {
    if (success) {
        b->consec_successes++;
        b->consec_failures = 0;
        if (b->consec_successes >= HEALTH_PASS_THRESHOLD) {
            b->state = BACKEND_HEALTHY;
        }
    } else {
        b->consec_failures++;
        b->consec_successes = 0;
        if (b->consec_failures >= HEALTH_FAIL_THRESHOLD) {
            b->state = BACKEND_UNHEALTHY;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 1. Three consecutive failures → UNHEALTHY                           */
/* ------------------------------------------------------------------ */
static void test_fail_threshold(void) {
    backend_t b;
    memset(&b, 0, sizeof(b));
    b.state = BACKEND_HEALTHY;

    apply_probe(&b, 0); /* failure 1 */
    check("fail_1: still HEALTHY after 1 failure",
          b.state == BACKEND_HEALTHY);

    apply_probe(&b, 0); /* failure 2 */
    check("fail_2: still HEALTHY after 2 failures",
          b.state == BACKEND_HEALTHY);

    apply_probe(&b, 0); /* failure 3 — threshold reached */
    check("fail_3: UNHEALTHY after 3 consecutive failures",
          b.state == BACKEND_UNHEALTHY);
}

/* ------------------------------------------------------------------ */
/* 2. Two consecutive successes on UNHEALTHY → HEALTHY                */
/* ------------------------------------------------------------------ */
static void test_pass_threshold(void) {
    backend_t b;
    memset(&b, 0, sizeof(b));
    b.state           = BACKEND_UNHEALTHY;
    b.consec_failures = HEALTH_FAIL_THRESHOLD; /* arrived here the hard way */

    apply_probe(&b, 1); /* success 1 */
    check("pass_1: still UNHEALTHY after 1 success",
          b.state == BACKEND_UNHEALTHY);

    apply_probe(&b, 1); /* success 2 — threshold reached */
    check("pass_2: HEALTHY after 2 consecutive successes",
          b.state == BACKEND_HEALTHY);
}

/* ------------------------------------------------------------------ */
/* 3. A single failure on HEALTHY does NOT immediately set UNHEALTHY  */
/* ------------------------------------------------------------------ */
static void test_single_fail_no_change(void) {
    backend_t b;
    memset(&b, 0, sizeof(b));
    b.state = BACKEND_HEALTHY;

    apply_probe(&b, 0);
    check("single_fail: HEALTHY backend stays HEALTHY after 1 failure",
          b.state == BACKEND_HEALTHY);
    check("single_fail: consec_failures is 1",
          b.consec_failures == 1);
}

/* ------------------------------------------------------------------ */
/* 4. Recovery resets consec_failures                                  */
/* ------------------------------------------------------------------ */
static void test_recovery_resets(void) {
    backend_t b;
    memset(&b, 0, sizeof(b));
    b.state           = BACKEND_UNHEALTHY;
    b.consec_failures = HEALTH_FAIL_THRESHOLD;

    apply_probe(&b, 1); /* success resets failure counter */
    check("recovery: consec_failures reset to 0 after success",
          b.consec_failures == 0);
    check("recovery: consec_successes is 1",
          b.consec_successes == 1);
}

/* ------------------------------------------------------------------ */
/* 5. Starting from UNKNOWN, HEALTH_PASS_THRESHOLD successes → HEALTHY */
/* ------------------------------------------------------------------ */
static void test_unknown_to_healthy(void) {
    backend_t b;
    memset(&b, 0, sizeof(b));
    b.state = BACKEND_UNKNOWN;

    for (int i = 0; i < HEALTH_PASS_THRESHOLD; i++) {
        apply_probe(&b, 1);
    }
    check("unknown_to_healthy: UNKNOWN → HEALTHY after threshold successes",
          b.state == BACKEND_HEALTHY);
}

/* ------------------------------------------------------------------ */
int main(void) {
    test_fail_threshold();
    test_pass_threshold();
    test_single_fail_no_change();
    test_recovery_resets();
    test_unknown_to_healthy();

    printf("test_health: all tests passed\n");
    return 0;
}
