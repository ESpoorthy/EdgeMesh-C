# EdgeMesh-C Agent Instructions

## Project goal
Build EdgeMesh-C: a C17 HTTP reverse proxy and load balancer with round-robin and least-connections policies, backend health checks, automatic failover, a local JSON admin API, actual metrics, a benchmark client and a React + TypeScript dashboard.

## Read first
Before implementing code, read:
- docs/requirements.md
- docs/architecture.md
- docs/api-contract.md
- docs/test-plan.md

Treat these documents as the shared source of truth for implementation, Sigma UI design and teammate work. If they contradict one another, report the contradiction and suggest a minimal documented resolution before coding.

## Technology and scope
- C17 is the networking implementation language.
- Use POSIX/BSD sockets and pthreads for the macOS MVP.
- Use CMake for the C build and CTest for automated tests.
- React + TypeScript is a separate dashboard and must consume the documented API.
- One C process owns two listeners: proxy at 127.0.0.1:8080 and admin API at 127.0.0.1:9090.
- Three local demo backends use 127.0.0.1:9001, :9002 and :9003.
- Support only the documented limited HTTP/1.1 GET subset. Do not claim full HTTP/1.1 compliance.
- Support only round_robin and least_connections in the MVP.
- Backend targets are fixed to the configured local pool. Never build an unrestricted forward proxy or accept arbitrary upstream URLs.
- TLS termination, HTTP/2, WebSockets, persistent connections, request-body forwarding, Kubernetes, cloud deployment and AI features are out of scope.

## Engineering requirements
- Make small, reviewable changes rather than generating the whole application in one pass.
- Compile and run relevant tests after each meaningful change. Report exact commands and actual results. Never claim a test or build ran unless it actually did.
- Use strong compiler warnings and resolve warnings introduced by changed code.
- Check all socket/system-call results and handle partial I/O, timeouts, disconnects and cleanup.
- Bound buffers, request sizes, concurrent connections, metrics history and admin body sizes.
- Avoid undefined behaviour, out-of-bounds access, unsafe string handling and unbounded allocation.
- Synchronise shared backend state, in-flight counts, policy and metrics. Do not hold shared-state locks during network I/O.
- Release the in-flight backend reservation on every success and failure path.
- Serialize valid JSON with escaping; never insert untrusted text into JSON without proper escaping.
- Keep metric semantics aligned with docs/api-contract.md. Do not fabricate live metrics or benchmark results.
- Bind both listeners to loopback and use only the documented dashboard CORS origins, without wildcard CORS. CORS is not authentication.
- Never allow API requests to read arbitrary files or execute commands.
- Keep secrets and machine-specific paths out of source control.

## Dashboard requirements
- Follow the approved Sigma screens when available.
- Keep React focused on presentation and API interaction. Routing decisions and proxy state belong in C.
- Use the paths and schemas from docs/api-contract.md; do not invent independent API routes.
- Clearly identify sample data before integration; after integration, all live metrics must come from the C API.
- Include loading, disconnected, error and empty-result states.
- Poll no faster than every two seconds in the MVP, and clean up timers/requests correctly.

## Planned build/test commands
After the CMake project exists, the intended workflow is:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

These commands are intended workflow, not evidence that the build already exists or has passed. Follow the actual build configuration and update the docs if it changes.

## End-of-task report
At the end of each task, report:
1. Files changed and why.
2. Design decisions and compatibility implications.
3. Commands actually run and their results.
4. Known limitations, untested paths and follow-up work.

Do not make unrelated changes, overwrite the MIT license, or expand the MVP without identifying the trade-off and updating documentation first.
