# EdgeMesh-C

A C17 HTTP reverse proxy and load balancer with round-robin and least-connections policies, backend health checks, automatic failover, an admin JSON API, a benchmark client, and a React + TypeScript dashboard.

This is a local educational prototype. It supports a limited HTTP/1.1 GET subset on loopback only. It is not production-ready.

---

## Contents

- [Prerequisites](#prerequisites)
- [Build](#build)
- [Run](#run)
- [Test](#test)
- [Benchmark](#benchmark)
- [Admin API curl examples](#admin-api-curl-examples)
- [Dashboard](#dashboard)
- [Known limitations](#known-limitations)
- [Documentation](#documentation)

---

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| WSL Ubuntu | 22.04 | All C build and test commands run inside WSL |
| GCC | ≥ 13 (tested with 15.2.0) | C17 with `-Wall -Wextra -Wpedantic -Werror` |
| CMake | ≥ 3.20 | Build system |
| CTest | bundled with CMake | Test runner |
| bash | ≥ 4 | Integration smoke test |
| curl | any recent | Integration test and curl examples |
| python3 | any | JSON validation in integration test (or `jq`) |
| Node.js | 20 | Dashboard only (Windows PowerShell, not WSL) |
| npm | bundled with Node.js | Dashboard only |

All C commands below must be run inside WSL (`wsl -e bash -c "..."` from PowerShell, or a WSL terminal directly).

---

## Build

```bash
# Configure (Debug build; add -DCMAKE_BUILD_TYPE=Release for a release build)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build all targets in parallel
cmake --build build --parallel
```

Produced binaries in `build/`:

| Binary | Description |
|---|---|
| `edgemesh` | Main reverse proxy + admin API process |
| `demo_backend` | Minimal HTTP/1.0 echo server (one instance per backend port) |
| `benchmark_client` | Multi-threaded HTTP load generator |

---

## Run

Open four terminal sessions (or background the first three):

**Terminal 1 — Demo backends (three instances)**

```bash
# Start all three demo backends
./build/demo_backend 9001 &
./build/demo_backend 9002 &
./build/demo_backend 9003 &
```

Each backend responds with `Backend at port <port>` on `127.0.0.1:<port>`.

**Terminal 2 — EdgeMesh proxy + admin API**

```bash
./build/edgemesh
```

- Proxy listener: `http://127.0.0.1:8080/`
- Admin API: `http://127.0.0.1:9090/`

Stop with `Ctrl+C` (SIGINT) or `kill <pid>`.

**Quick sanity check**

```bash
curl http://127.0.0.1:8080/
# → Backend at port 9001 (or 9002, 9003 — rotating round-robin)
```

---

## Test

### Unit tests (no network required)

```bash
ctest --test-dir build --output-on-failure -R "test_http_parser|test_selector|test_health|test_metrics|test_admin"
```

| Test | What it covers |
|---|---|
| `test_http_parser` | HTTP GET parsing, malformed input, oversized headers, wrong method |
| `test_selector` | Round-robin rotation, least-connections, unhealthy skip, release_backend |
| `test_health` | Health state machine thresholds (no network) |
| `test_metrics` | Percentile calculations, ring buffer bounds, time-series rotation |
| `test_admin` | JSON escaping, policy validation, json_build_* output structure |

### Integration smoke test (requires loopback network, ~60 s)

The integration test starts the real binaries, probes the proxy and admin API end-to-end, and exercises failover and recovery:

```bash
# Run only the integration smoke test
ctest --test-dir build --output-on-failure -R integration_smoke
```

What the test covers (INT-01 through INT-10 from `docs/test-plan.md`):

| Check | Scenario |
|---|---|
| INT-01 | All four processes start and bind successfully |
| INT-02 | Proxy forwards GET and returns backend identity |
| INT-03 | Round-robin rotates across all three backends over 6 requests |
| INT-10a | PUT /api/v1/policy with `least_connections` returns 200 |
| INT-10b | PUT /api/v1/policy with invalid value returns 400 |
| INT-10c | All 6 admin GET endpoints return 200 + valid JSON |
| INT-05 | Kill one backend → marked unhealthy after 3 probe failures (~18 s) |
| INT-05b | Remaining two backends continue serving (not 503) |
| INT-07 | Kill all backends → proxy returns 503 |
| INT-06 | Restart backends → recovery after 2 probe successes (~12 s) |

### Run all tests

```bash
ctest --test-dir build --output-on-failure
```

---

## Benchmark

The proxy must be running and at least one backend must be healthy before running the benchmark.

```bash
# 1000 requests, 10 concurrent workers
./build/benchmark_client -n 1000 -c 10 -t http://127.0.0.1:8080/

# 5000 requests, 20 concurrent workers
./build/benchmark_client -n 5000 -c 20 -t http://127.0.0.1:8080/
```

Options:

| Flag | Default | Max | Description |
|---|---|---|---|
| `-n <count>` | 1000 | 100 000 | Total requests to send |
| `-c <concurrency>` | 10 | 256 | Concurrent worker threads |
| `-t <url>` | required | — | Target URL (must be `http://127.0.0.1:<port>/…`) |

Results are written to `results/latest.json` and `results/latest.csv` in the directory alongside the binary. The admin API exposes the latest run at `GET /api/v1/benchmarks/latest`.

---

## Admin API curl examples

All endpoints are on `http://127.0.0.1:9090`. Edgemesh must be running.

### GET /api/v1/health

```bash
curl -s http://127.0.0.1:9090/api/v1/health | python3 -m json.tool
```

```json
{
  "status": "ok",
  "service": "edgemesh-c",
  "version": "0.1.0",
  "uptime_seconds": 42
}
```

### GET /api/v1/overview

```bash
curl -s http://127.0.0.1:9090/api/v1/overview | python3 -m json.tool
```

Returns proxy state, policy, and aggregate metrics snapshot.

### GET /api/v1/backends

```bash
curl -s http://127.0.0.1:9090/api/v1/backends | python3 -m json.tool
```

Returns an array of all three configured backends with their health state, connection counts, and last probe result.

### PUT /api/v1/policy

```bash
# Switch to least-connections
curl -s -X PUT \
  -H "Content-Type: application/json" \
  -d '{"policy":"least_connections"}' \
  http://127.0.0.1:9090/api/v1/policy | python3 -m json.tool

# Switch back to round-robin
curl -s -X PUT \
  -H "Content-Type: application/json" \
  -d '{"policy":"round_robin"}' \
  http://127.0.0.1:9090/api/v1/policy | python3 -m json.tool
```

Allowed values: `round_robin`, `least_connections`. Invalid values return 400 without changing the active policy.

### GET /api/v1/metrics

```bash
# Default 60-second window
curl -s "http://127.0.0.1:9090/api/v1/metrics" | python3 -m json.tool

# Custom window (10–300 seconds)
curl -s "http://127.0.0.1:9090/api/v1/metrics?window_seconds=30" | python3 -m json.tool
```

Returns per-second time-series data points within the requested window. Empty `points` array is valid on startup.

### GET /api/v1/benchmarks/latest

```bash
curl -s http://127.0.0.1:9090/api/v1/benchmarks/latest | python3 -m json.tool
```

Returns `{"available": false, "run": null}` when no benchmark has been run yet, or the full result object after a completed `benchmark_client` run.

---

## Dashboard

The React + TypeScript dashboard lives in `dashboard/`. These commands run in **Windows PowerShell** (not WSL):

```powershell
# Install dependencies (first time only)
cd dashboard
npm install

# Start the development server (proxies /api/* to http://127.0.0.1:9090)
npm run dev
```

Open `http://localhost:5173` in a browser. The edgemesh process must be running for live data.

For a production build:

```powershell
npm run build
```

---

## Known limitations

- **HTTP/1.1 subset only.** Only origin-form GET requests are supported. POST, PUT, DELETE and other methods return 405. Request bodies are not forwarded.
- **No TLS.** All connections are plain HTTP. Do not expose these ports to untrusted networks.
- **No persistent connections.** Every proxied request uses a new TCP connection to the backend (`Connection: close`).
- **Loopback only.** Both listeners (`127.0.0.1:8080` and `127.0.0.1:9090`) bind to loopback. External hosts cannot connect.
- **Fixed backend pool.** The three backends (`127.0.0.1:9001/9002/9003`) are configured at compile time. Dynamic backend registration is not supported.
- **Connection limits.** Max 128 concurrent proxy connections, max 16 concurrent admin connections. Excess connections receive 503 immediately.
- **No HTTP/2, WebSockets, or chunked encoding.** These are out of scope for the MVP.
- **Linux/WSL only.** Uses POSIX/BSD sockets and pthreads. macOS may work but is not tested in this environment.
- **Educational prototype.** Not suitable for production use.

---

## Documentation

| File | Contents |
|---|---|
| `docs/requirements.md` | Functional and non-functional requirements, HTTP status mapping |
| `docs/architecture.md` | Module design, timeout constants, health thresholds, metrics semantics |
| `docs/api-contract.md` | Admin API schema, field definitions, CORS policy |
| `docs/test-plan.md` | Unit, integration and benchmark test strategy |
| `AGENTS.md` | Agent/contributor instructions |

---

## License

MIT — see [LICENSE](LICENSE).
