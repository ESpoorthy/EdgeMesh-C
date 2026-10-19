#include "edgemesh/selector.h"
#include "edgemesh/log.h"

#include <limits.h>

/*
 * select_backend — selects a healthy backend per the pool's current policy.
 *
 * Round-robin:       rotates among HEALTHY backends starting after last_rr_idx.
 * Least-connections: picks the HEALTHY backend with the fewest active_connections;
 *                    ties broken by lower index.
 *
 * Increments active_connections under the pool lock before returning.
 * Sets *out_idx on success.
 *
 * Returns  0 on success.
 * Returns -1 if no healthy backend is available.
 */
int select_backend(backend_pool_t *pool, int *out_idx) {
    pthread_mutex_lock(&pool->mutex);

    int selected = -1;

    if (pool->policy == POLICY_ROUND_ROBIN) {
        /*
         * Iterate BACKEND_POOL_SIZE times starting from the position after
         * last_rr_idx, skipping UNHEALTHY backends.
         */
        for (int i = 0; i < BACKEND_POOL_SIZE; i++) {
            int candidate = (pool->last_rr_idx + 1 + i) % BACKEND_POOL_SIZE;
            if (pool->backends[candidate].state == BACKEND_HEALTHY) {
                selected = candidate;
                break;
            }
        }
        if (selected >= 0) {
            pool->last_rr_idx = selected;
        }
    } else {
        /* POLICY_LEAST_CONNECTIONS */
        int min_active = INT_MAX;
        for (int i = 0; i < BACKEND_POOL_SIZE; i++) {
            if (pool->backends[i].state != BACKEND_HEALTHY) continue;
            if (pool->backends[i].active_connections < min_active) {
                min_active = pool->backends[i].active_connections;
                selected   = i;
            }
        }
    }

    if (selected < 0) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    pool->backends[selected].active_connections++;
    *out_idx = selected;

    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

/*
 * release_backend — decrements active_connections for backend[idx] under lock.
 * Count is never decremented below 0.
 * Must be called on every success AND failure path after select_backend().
 */
void release_backend(backend_pool_t *pool, int idx) {
    if (idx < 0 || idx >= BACKEND_POOL_SIZE) return;

    pthread_mutex_lock(&pool->mutex);
    if (pool->backends[idx].active_connections > 0) {
        pool->backends[idx].active_connections--;
    }
    pthread_mutex_unlock(&pool->mutex);
}
