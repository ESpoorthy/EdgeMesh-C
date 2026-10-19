#include "edgemesh/backend.h"
#include "edgemesh/log.h"

#include <string.h>

/* Global backend pool — shared across all modules. */
backend_pool_t g_pool;

/*
 * pool_init — initialises g_pool with three backends at :9001/:9002/:9003.
 * Must be called once before any other pool function.
 */
void pool_init(void) {
    memset(&g_pool, 0, sizeof(g_pool));

    pthread_mutex_init(&g_pool.mutex, NULL);
    pthread_mutex_init(&g_pool.policy_mutex, NULL);

    g_pool.policy      = POLICY_ROUND_ROBIN;
    g_pool.last_rr_idx = -1; /* start before the first backend */

    int ports[BACKEND_POOL_SIZE] = {9001, 9002, 9003};
    for (int i = 0; i < BACKEND_POOL_SIZE; i++) {
        g_pool.backends[i].id    = i;
        g_pool.backends[i].port  = ports[i];
        g_pool.backends[i].state = BACKEND_UNKNOWN;
        strncpy(g_pool.backends[i].address, "127.0.0.1",
                sizeof(g_pool.backends[i].address) - 1);
    }

    log_info_s("pool_init: 3 backends initialised (state=UNKNOWN)");
}

/* pool_lock / pool_unlock — convenience wrappers around g_pool.mutex. */
void pool_lock(void) {
    pthread_mutex_lock(&g_pool.mutex);
}

void pool_unlock(void) {
    pthread_mutex_unlock(&g_pool.mutex);
}

/*
 * pool_get_backend — returns a pointer to backend[idx].
 * idx is bounds-checked; returns NULL if out of range.
 * Must NOT be called while performing network I/O with the lock held.
 */
backend_t *pool_get_backend(int idx) {
    if (idx < 0 || idx >= BACKEND_POOL_SIZE) {
        return NULL;
    }
    return &g_pool.backends[idx];
}
