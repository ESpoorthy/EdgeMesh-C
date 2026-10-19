#ifndef EDGEMESH_PROXY_H
#define EDGEMESH_PROXY_H

#include "edgemesh/backend.h"
#include "edgemesh/metrics.h"

/* Connection limits (from architecture.md §5). */
#define MAX_PROXY_CONNECTIONS 128
#define MAX_ADMIN_CONNECTIONS  16

/*
 * proxy_start(pool, metrics)
 *
 * Binds to 127.0.0.1:8080.  Spawns a thread per accepted connection up to
 * MAX_PROXY_CONNECTIONS; excess connections receive 503 immediately.
 * Does not return until proxy_stop() is called.
 */
void proxy_start(backend_pool_t *pool, metrics_t *metrics);

/*
 * proxy_stop()
 *
 * Signals the proxy accept loop to shut down and joins all worker threads.
 */
void proxy_stop(void);

#endif /* EDGEMESH_PROXY_H */
