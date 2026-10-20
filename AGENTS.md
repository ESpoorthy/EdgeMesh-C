# EdgeMesh-C Agent Instructions

## Project goal and source of truth
Build EdgeMesh-C as a C17 HTTP reverse proxy and load balancer with two policies, three fixed local demo backends, health checks and failover, a local JSON admin API, actual metrics, a C benchmark client and a React + TypeScript dashboard.

Before implementation, read all four documents:
- `docs/requirements.md`
- `docs/architecture.md`
- `docs/api-contract.md`
- `docs/test-plan.md`

They are the shared source of truth for implementation, Sigma UI/UX design and teammate work. If they conflict, report the conflict and propose a minimal resolution before coding; update all affected documents together.

## Technology and scope

- Networking, API, demo backends and benchmark: C17; macOS POSIX/BSD sockets and pthreads.
- Build and automated tests: CMake and CTest.
- Dashboard: separate React + TypeScript application that consumes only the documented API.
- One C process owns proxy `127.0.0.1:8080` and admin `127.0.0.1:9090`; demo backends default to ports 9001–9003.
- Only documented limited HTTP/1.1 GET requests are supported. Never claim complete HTTP/1.1 conformance.
- Only `round_robin` and `least_connections` are MVP policies.
- Targets are a fixed three-entry loopback pool. Never accept arbitrary destinations or make an open proxy.
- TLS termination, HTTP/2, WebSockets, request bodies, persistent connections, Kubernetes, cloud deployment and AI are out of scope.

## Engineering rules

- Make small, reviewable changes. Do not generate the whole application in one pass.
- Compile and run relevant tests after each meaningful implementation change. Report exact commands and actual results; never claim tests/builds that did not run.
- Use strong compiler warnings. Check socket/system-call results and handle partial I/O, EINTR, timeouts, disconnects, shutdown and cleanup.
- Bound buffers, request/header/body sizes, connections/workers, benchmark parameters and retained metrics. Avoid undefined behavior, unsafe string operations and unchecked allocation.
- Synchronize backend state, selected policy, in-flight reservations and metrics. Never hold shared-state locks during network I/O. Release every reservation exactly once on all paths.
- Serialize valid JSON with correct escaping. Never insert untrusted text directly into JSON, logs or response headers.
- Keep metric meanings aligned with `docs/api-contract.md`. Live metrics and benchmark records must be measured, never fabricated.
- Bind listeners to IPv4 loopback. Use only documented CORS origins and no wildcard. CORS is not authentication.
- Admin routes must not read caller-selected files or execute commands. Benchmark loading is restricted to its fixed documented result path.
- Do not overwrite the MIT `LICENSE`, include secrets or machine-specific paths, or expand scope without documenting the trade-off.

## Dashboard rules

Follow the approved Sigma screens and Figma design contract in `docs/requirements.md`, `docs/architecture.md` and the React + TypeScript Handoff frame. The runtime has exactly three screens: Overview (`/`), Backend Health (`/backends`) and Benchmark Lab (`/benchmarks`); Design System and handoff are references, not routes. Preserve the dark technical visual identity and shared design tokens. Keep routing/proxy state in C and presentation/API access in React. Use only the six documented backend endpoints; do not invent routes. Keep design/test fixtures visibly labeled and isolated from live data. Never invent `Recovered` as a server state or API field; a transient UI recovery cue requires observing unhealthy then healthy. Distinguish admin API health, proxy readiness, backend health and client-observed API connectivity. Include loading, stale, disconnected/error, empty/no-result and unhealthy/unknown states. Poll no faster than once every two seconds and clean up timers/requests.

## Planned build/test workflow

After CMake is implemented, intended commands are:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

These commands are a plan, not evidence of success. Follow actual targets/options and keep `docs/test-plan.md` aligned.

## End-of-task report

At the end of each task, report:

1. Files changed and why.
2. Design decisions and compatibility implications.
3. Commands actually run and their results.
4. Known limitations, untested paths and follow-up work.

Do not claim execution results without evidence. If project instructions or the user say not to run tests, follow that instruction and state that no tests were run.
