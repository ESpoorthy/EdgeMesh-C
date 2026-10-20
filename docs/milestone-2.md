# Milestone 2: routing and resilience

This milestone adds the fixed three-backend pool, the two selection policies, bounded concurrent proxy workers, active health probes, and failover/recovery behaviour. The full MVP requirements and API contract remain authoritative; M2 does not implement the administrative API, metrics, benchmark client, or dashboard.

## Implemented scope

- Three fixed IPv4-loopback backend slots. The default ports are 9001, 9002 and 9003; the test/demo `--backend-port BASE_PORT` option chooses three consecutive loopback ports and rejects a range that would overflow 65535.
- Round Robin selects the next healthy backend in stable pool order and skips entries not currently healthy.
- Least Connections selects the healthy backend with the fewest reserved in-flight requests; ties rotate through a cursor for deterministic distribution.
- Selection and in-flight reservation occur under one mutex. The reservation is released on every upstream result/error path. Proxy handlers run in detached pthread workers with a cap of 128 admitted client sockets; at capacity, an accepted socket is closed without an HTTP response.
- A dedicated health worker probes each backend with `GET /health` on a one-second schedule and a 250 ms total probe deadline. The state starts `unknown`, changes to `unhealthy` after three consecutive failed probes, and changes to `healthy` after two consecutive successful probes. Probe network I/O is outside the pool mutex.
- Upstream attempts have a 2-second deadline, each request has the existing 9-second outer deadline, and each backend is attempted at most once per client request. An upstream response is buffered and validated before the first client response byte, so an upstream connection/protocol failure can be retried against another currently healthy candidate. If no backend is healthy at initial selection, the proxy returns 503; if attempted backends fail, it returns 502. An upstream HTTP error response is forwarded as an upstream response, not treated as a connection failure.
- The proxy policy can be selected at process start using `--policy round_robin` (default) or `--policy least_connections`. M3 will expose the synchronized policy update through the documented admin API.

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

Switch initial policy with `--policy least_connections`. Give health checks roughly two successful probe intervals before expecting requests to be routed; backend states start as `unknown`.

## Verification

The automated tests cover policy selector/reservation logic and a local integration flow for balanced traffic, malformed requests, backend stop/failover, backend restart/recovery and an all-unhealthy pool. These tests must pass on the macOS warnings-as-errors build before this milestone is considered verified.
