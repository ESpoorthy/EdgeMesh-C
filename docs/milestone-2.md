# Milestone 2: routing and resilience

M2 adds the fixed three-backend pool, both selection policies, bounded concurrent proxy workers, active health probes, and failover/recovery behaviour. The full MVP requirements and API contract remain authoritative; M2 does not implement the administrative API, process metrics/time series, benchmark client or dashboard.

## Implemented scope

- Three fixed IPv4-loopback backend slots. Default ports are 9001, 9002 and 9003; the demo/test `--backend-port BASE_PORT` option chooses three consecutive loopback ports and rejects a range that would overflow 65535.
- Round Robin selects the next healthy backend in stable pool order and skips entries not currently healthy.
- Least Connections selects the healthy backend with the fewest reserved in-flight requests; ties rotate through a cursor for deterministic distribution.
- Selection and in-flight reservation occur under one mutex. Reservations are released on upstream success/failure paths. Proxy handlers run in detached pthread workers with a cap of 128 admitted client sockets; at capacity, an accepted socket is closed without an HTTP response.
- A dedicated health worker probes each backend with `GET /health` on a one-second schedule with a 250 ms total probe deadline. State starts `unknown`, changes to `unhealthy` after three consecutive failed probes, and changes to `healthy` after two consecutive successful probes. Probe network I/O happens outside the pool mutex. Each completed probe updates the last-check timestamp and latency in a synchronized backend snapshot.
- Each request can attempt each backend at most once. Upstream operations, response reading, sanitization and downstream response writing share a 2-second per-attempt deadline; the request keeps its 9-second outer deadline. Responses are bounded and validated before any response bytes are written to the client, so a failure before output can retry another currently healthy candidate. If no backend is healthy at initial selection, the proxy returns 503; if attempted candidates fail, it returns 502. A valid upstream HTTP error response is forwarded rather than treated as an upstream connection failure.
- Requests forward safe end-to-end headers, rewrite `Host` to the selected configured backend, and remove hop-by-hop fields including headers nominated by `Connection`. Responses similarly filter hop-by-hop headers and declare `Connection: close`.
- Policy is chosen at process startup with `--policy round_robin` (default) or `--policy least_connections`. M3 will expose synchronized policy updates through the documented admin API.

## Run

Start the backends in three terminals:

```sh
./build/edgemesh-backend --port 9001 --id backend-1
./build/edgemesh-backend --port 9002 --id backend-2
./build/edgemesh-backend --port 9003 --id backend-3
```

Then run the proxy:

```sh
./build/edgemesh-proxy --policy round_robin
curl --include http://127.0.0.1:8080/demo
```

Switch the initial policy with `--policy least_connections`. Backend states begin as `unknown`; allow about two successful probe intervals before expecting traffic to be routed to a backend.

## Verification

Verified by GitHub Actions on the latest M2 source commit (`d9db3f6d0e744ba484d7a66784eef8ff3d2816c1)) using the `macos-14` runner and AppleClang 15.0.0.15000309. The exact workflow run is [C macOS build and tests](https://github.com/ESpoorthy/EdgeMesh-C/actions/runs/39384767128).

Commands executed by the workflow:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DEDGEMESH_ENABLE_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Observed result: configure succeeded; the build succeeded with warnings treated as errors; CTest passed **4/4**:
- `http_parser`
- `backend_pool_unit`
- `proxy_integration_round_robin`
- `proxy_integration_least_connections`

The integration tests cover equal round-robin distribution, selection/reservation and health thresholds, backend stop/failover/restart/recovery, all-backends-down behavior, excess connections being closed without a response, safe request-header forwarding, hop-by-hop response filtering and malformed request rejection. These results come from GitHub Actions; a local build was not run in the authoring environment.

## Known limits / next milestone

- Policy updates are startup-only here. M3 should add the synchronized `PUT /api/v1/policy` path.
- Admin API endpoints, process-level counters/time-series, benchmark CLI/results and dashboard are not implemented in M2.
- This remains a limited educational HTTP/1.1 GET proxy, not a production reverse proxy.
