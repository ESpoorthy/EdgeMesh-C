# EdgeMesh-C Architecture

**Version:** 1.0 — MVP  
**Platform:** macOS, C17 and POSIX sockets

## 1. System topology

    React + TypeScript dashboard (localhost:5173)
                    |
                    | JSON over HTTP, allowed local CORS origin
                    v
    Admin API listener — C application — 127.0.0.1:9090
                    |
                    v
          Bounded metrics/state snapshots

    Browser or C benchmark client
                    |
                    | HTTP GET
                    v
    Reverse-proxy listener — same C process — 127.0.0.1:8080
                    |
                    v
           Backend selector
       Round robin / Least connections
                    |
              Configured pool
             /      |      \
            v       v       v
       Backend 1 Backend 2 Backend 3
       :9001     :9002     :9003

    Health-check worker ---> backend state and metrics
    C benchmark client ----> local JSON/CSV result files
    Admin API -------------> reads latest completed benchmark result

The proxy and admin API are separate listeners and handlers in one C application process. Demo backends are separate local server processes. The dashboard is a separate development application. The benchmark client is a separate C command-line program.

## 2. Module responsibilities

| Module | Responsibility |
|---|---|
| src/proxy | Accept client connections, enforce connection limits and coordinate proxy requests |
| src/http | Parse the supported request subset, validate headers and produce HTTP errors |
| src/backend | Fixed backend pool, eligible-backend selection and thread-safe in-flight counters |
| src/health | Periodic bounded probes and state transitions using success/failure thresholds |
| src/metrics | Actual counters, latency samples, throughput and bounded time-series history |
| src/admin_api | Admin HTTP routes, JSON validation/serialization and policy updates |
| src/common | Shared bounded I/O, timestamps, logging and error helpers where useful |
| src/demo_backend | Small demo server returning its backend identity and providing controlled health behaviour |
| benchmark | C load generator and JSON/CSV result writer |
| dashboard | React + TypeScript screens that display API data without owning proxy state |
| tests | Unit, integration and failure-scenario tests |

The exact file split may evolve, but API and module contracts must remain consistent with the documentation.

## 3. Proxy request lifecycle

1. Accept a TCP client on 127.0.0.1:8080, respecting the configured connection limit.
2. Read until the end of the request headers with a timeout and a 16 KiB maximum header size.
3. Validate request method, target, version and header syntax. Accept only origin-form GET requests in the MVP.
4. Select a healthy backend according to the current policy. If none is healthy, send 503 Service Unavailable.
5. Reserve the selected backend's in-flight count atomically with the selection decision.
6. Connect to the fixed backend with bounded timeouts; forward the GET path/query and controlled headers. Do not blindly forward hop-by-hop headers.
7. Validate the upstream status/header section within a bounded size, then forward the response while handling partial writes and timeouts.
8. Close sockets and release the in-flight reservation on every success and failure path.
9. Record outcome, response bytes and latency, then write a structured log entry.

This is not a complete HTTP implementation. The MVP is one request per client connection with connection-close behaviour. Request bodies, chunked request bodies, upgrades, CONNECT and persistent connections are excluded. For the HTTP status code returned at each failure point, see the HTTP status mapping table in docs/requirements.md.

## 4. Backend state and selection

Each backend record contains a stable ID, fixed loopback address, health state, consecutive probe counts, last-check time, optional probe latency, in-flight request count and request/failure counters.

**Round robin:** Rotate through the configured list and skip unhealthy backends.

**Least connections:** Choose the healthy backend with the smallest in-flight count. Use deterministic rotation among ties to prevent persistent first-entry preference.

The selector and in-flight reservation must share a synchronisation strategy so concurrent workers cannot choose a backend using a stale count. Decrement the count on all exit paths. Never hold the backend lock during network I/O.

Health checks run in a worker or equivalent scheduled mechanism. They use bounded timeouts and configurable thresholds; a single transient probe failure must not necessarily remove a backend immediately.

#### Health thresholds

Consecutive failures to mark a backend unhealthy: **3**. Consecutive successes to recover a backend to healthy: **2**. Probe interval: **5 s**. A transient single probe failure does not immediately remove a backend from service.

## 5. Concurrency and resource limits

A bounded pthread worker model is sufficient for the MVP. Proxy work, health probes and admin requests must not block one another indefinitely.

- Protect backend state, active counts, selected policy and metrics with scoped mutexes or equivalent.
- Do not hold shared-state locks while waiting on socket I/O.
- Define an explicit maximum active connection count and predictable overload behaviour.
- Maximum concurrent proxy connections: **128**; when this limit is reached, new client connections are accepted and immediately receive HTTP 503 then closed. Maximum concurrent admin connections: **16**; excess connections are accepted and immediately receive HTTP 503 then closed.
- Bound all buffers, request sizes, latency sample storage and time-series history.
- Handle signals and shut down listeners/workers cleanly.
- Keep a single, auditable cleanup path for sockets, buffers and in-flight reservations.

## 5a. Timeout and retry constants

| Constant | Value |
|---|---|
| Client read timeout (request headers) | 10 s |
| Backend connect timeout | 5 s |
| Backend read timeout (response headers) | 10 s |
| Overall per-request deadline | 20 s |
| Health probe connect timeout | 3 s |
| Health probe read timeout | 3 s |
| Admin request read timeout | 10 s |

Retry policy: no retry on failure; each request makes exactly one attempt to each backend. See the HTTP status mapping in docs/requirements.md for the status code returned when each timeout fires.

## 6. Admin API and dashboard

The admin API exposes only the six endpoints in api-contract.md. The policy endpoint accepts only the two enumerated policy values. The API does not execute commands or accept arbitrary backend addresses.

- Bind only to 127.0.0.1:9090.
- Limit admin headers to 16 KiB and request bodies to 4 KiB.
- Support OPTIONS preflight for the dashboard development origins.
- Permit only http://localhost:5173 and http://127.0.0.1:5173 through browser CORS.
- Do not treat CORS as authentication; local processes can make direct requests to a loopback service.

The benchmark tool saves actual results to results/latest.json, resolved relative to the repository root by deriving the path from the executable's own path at startup (not from the process working directory). The admin API reads only that fixed path — results/latest.json relative to the same executable-derived root — and never accepts a caller-supplied path.

The dashboard polls at two-second intervals, cleans up requests/timers when screens unmount, and renders loading, error, disconnected and empty states. Every integrated live metric must come from the API, not hardcoded mock values.

## 7. Target repository layout

    EdgeMesh-C/
    ├── AGENTS.md
    ├── CMakeLists.txt
    ├── README.md
    ├── LICENSE
    ├── .gitignore
    ├── docs/
    │   ├── requirements.md
    │   ├── architecture.md
    │   ├── api-contract.md
    │   └── test-plan.md
    ├── include/
    ├── src/
    │   ├── proxy/
    │   ├── http/
    │   ├── backend/
    │   ├── health/
    │   ├── metrics/
    │   ├── admin_api/
    │   ├── common/
    │   └── demo_backend/
    ├── tests/
    ├── benchmark/
    ├── dashboard/
    └── results/

This is a target layout. Create source files and directories as implementation needs them; empty directories alone are not an implementation milestone.

## 8. Decisions and trade-offs

- C is the networking core; TypeScript is presentation only.
- One C process owns two separate listeners to reduce process-management overhead.
- Backends are fixed local targets to avoid turning the prototype into an open proxy.
- Runtime metrics must be measured, not simulated.
- The HTTP subset is intentionally small to keep parsing and forwarding testable within the deadline.
- No container, cloud or production deployment is an MVP dependency.
