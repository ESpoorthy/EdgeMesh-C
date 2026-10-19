#ifndef EDGEMESH_ADMIN_HANDLER_H
#define EDGEMESH_ADMIN_HANDLER_H

/*
 * handle_admin_request(client_fd)
 *
 * Reads an HTTP request from client_fd, dispatches to the appropriate admin
 * API handler, and writes the response.  Closes client_fd before returning.
 *
 * Supported routes (GET unless noted):
 *   GET  /api/v1/health
 *   GET  /api/v1/overview
 *   GET  /api/v1/backends
 *   PUT  /api/v1/policy
 *   GET  /api/v1/metrics[?window_seconds=N]
 *   GET  /api/v1/benchmarks/latest
 */
void handle_admin_request(int client_fd);

#endif /* EDGEMESH_ADMIN_HANDLER_H */
