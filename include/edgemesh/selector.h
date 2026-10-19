#ifndef EDGEMESH_SELECTOR_H
#define EDGEMESH_SELECTOR_H

#include "edgemesh/backend.h"

/*
 * select_backend(pool, out_idx)
 *
 * Selects a healthy backend according to the pool's current policy:
 *   - POLICY_ROUND_ROBIN:        rotating among healthy backends.
 *   - POLICY_LEAST_CONNECTIONS:  backend with fewest active_connections;
 *                                ties broken by lower index.
 *
 * Increments the selected backend's active_connections under the pool lock.
 * Sets *out_idx to the selected index.
 *
 * Returns  0 on success.
 * Returns -1 if no healthy backend is available.
 *
 * The caller MUST call release_backend() on every exit path after success.
 */
int select_backend(backend_pool_t *pool, int *out_idx);

/*
 * release_backend(pool, idx)
 *
 * Decrements the backend's active_connections under the pool lock.
 * The count is never decremented below 0.
 * Must be called on every success AND failure path after select_backend().
 */
void release_backend(backend_pool_t *pool, int idx);

#endif /* EDGEMESH_SELECTOR_H */
