#include "edgemesh/backend.h"
#include "edgemesh/selector.h"

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

/* ------------------------------------------------------------------ */
/* Setup helper: reset pool with all three backends HEALTHY            */
/* ------------------------------------------------------------------ */
static void reset_pool_healthy(backend_pool_t *pool) {
    pool_init(); /* re-initialises g_pool */
    pool_lock();
    for (int i = 0; i < BACKEND_POOL_SIZE; i++) {
        pool->backends[i].state              = BACKEND_HEALTHY;
        pool->backends[i].active_connections = 0;
    }
    pool->last_rr_idx = -1;
    pool->policy      = POLICY_ROUND_ROBIN;
    pool_unlock();
}

/* ------------------------------------------------------------------ */
/* 1. Round-robin cycles through 3 healthy backends                   */
/* ------------------------------------------------------------------ */
static void test_rr_cycles(void) {
    reset_pool_healthy(&g_pool);
    int counts[BACKEND_POOL_SIZE] = {0};

    for (int i = 0; i < 9; i++) {
        int idx = -1;
        int rc  = select_backend(&g_pool, &idx);
        assert(rc == 0);
        assert(idx >= 0 && idx < BACKEND_POOL_SIZE);
        counts[idx]++;
        release_backend(&g_pool, idx);
    }

    check("rr_cycles: each backend selected 3 times",
          counts[0] == 3 && counts[1] == 3 && counts[2] == 3);
}

/* ------------------------------------------------------------------ */
/* 2. Round-robin skips unhealthy backend                              */
/* ------------------------------------------------------------------ */
static void test_rr_skips_unhealthy(void) {
    reset_pool_healthy(&g_pool);
    /* Mark backend[1] UNHEALTHY. */
    pool_lock();
    g_pool.backends[1].state = BACKEND_UNHEALTHY;
    pool_unlock();

    int counts[BACKEND_POOL_SIZE] = {0};
    for (int i = 0; i < 6; i++) {
        int idx = -1;
        int rc  = select_backend(&g_pool, &idx);
        assert(rc == 0);
        counts[idx]++;
        release_backend(&g_pool, idx);
    }

    check("rr_skips: backend[1] never selected",
          counts[1] == 0 && counts[0] + counts[2] == 6);
}

/* ------------------------------------------------------------------ */
/* 3. Least-connections picks backend with min active                 */
/* ------------------------------------------------------------------ */
static void test_lc_picks_min(void) {
    reset_pool_healthy(&g_pool);
    g_pool.policy = POLICY_LEAST_CONNECTIONS;

    /* Manually set active counts (under lock). */
    pool_lock();
    g_pool.backends[0].active_connections = 5;
    g_pool.backends[1].active_connections = 2; /* min */
    g_pool.backends[2].active_connections = 8;
    pool_unlock();

    int idx = -1;
    int rc  = select_backend(&g_pool, &idx);
    check("lc_min: selects backend[1] (min active)",
          rc == 0 && idx == 1);
    if (rc == 0) release_backend(&g_pool, idx);
}

/* ------------------------------------------------------------------ */
/* 4. Least-connections tie-break: lower index wins                   */
/* ------------------------------------------------------------------ */
static void test_lc_tiebreak(void) {
    reset_pool_healthy(&g_pool);
    g_pool.policy = POLICY_LEAST_CONNECTIONS;

    pool_lock();
    g_pool.backends[0].active_connections = 3;
    g_pool.backends[1].active_connections = 3;
    g_pool.backends[2].active_connections = 3;
    pool_unlock();

    int idx = -1;
    int rc  = select_backend(&g_pool, &idx);
    check("lc_tiebreak: selects backend[0] (lowest index)",
          rc == 0 && idx == 0);
    if (rc == 0) release_backend(&g_pool, idx);
}

/* ------------------------------------------------------------------ */
/* 5. release_backend decrements count                                 */
/* ------------------------------------------------------------------ */
static void test_release_decrements(void) {
    reset_pool_healthy(&g_pool);

    int idx = -1;
    select_backend(&g_pool, &idx);
    assert(idx >= 0);

    pool_lock();
    int after_select = g_pool.backends[idx].active_connections;
    pool_unlock();

    release_backend(&g_pool, idx);

    pool_lock();
    int after_release = g_pool.backends[idx].active_connections;
    pool_unlock();

    check("release_decrements: count goes down by 1",
          after_release == after_select - 1);
}

/* ------------------------------------------------------------------ */
/* 6. No healthy backends → returns -1                                 */
/* ------------------------------------------------------------------ */
static void test_no_healthy(void) {
    reset_pool_healthy(&g_pool);
    pool_lock();
    for (int i = 0; i < BACKEND_POOL_SIZE; i++) {
        g_pool.backends[i].state = BACKEND_UNHEALTHY;
    }
    pool_unlock();

    int idx = -1;
    int rc  = select_backend(&g_pool, &idx);
    check("no_healthy: returns -1", rc == -1);
}

/* ------------------------------------------------------------------ */
int main(void) {
    test_rr_cycles();
    test_rr_skips_unhealthy();
    test_lc_picks_min();
    test_lc_tiebreak();
    test_release_decrements();
    test_no_healthy();

    printf("test_selector: all tests passed\n");
    return 0;
}
