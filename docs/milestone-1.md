# Milestone 1: networking foundation

This note records the runnable scope currently implemented. The reconciled MVP specification in the other four documents remains authoritative for the complete project; features explicitly deferred here are not represented as complete.

## Requirements and limits in this slice

- C17 / POSIX sockets, CMake, and CTest. The proxy defaults to `127.0.0.1:8080` and forwards only to the fixed `127.0.0.1` backend host (default port `9001`). The backend defaults to `127.0.0.1:9001`, with a configurable loopback port and one of the fixed IDs `backend-1`, `backend-2`, or `backend-3`.
- One HTTP request per client connection: origin-form `GET`, HTTP/1.1, exactly one non-empty `Host`, no fragment, and no request body or transfer encoding. The proxy sends a rewritten backend `Host` and `Connection: close`; it does not forward client headers in this milestone.
- Request line limit: 8 KiB; complete header section limit: 16 KiB. Client header deadline: 2 seconds. An accepted request has a 9-second outer deadline; the one upstream attempt has a 2-second deadline. I/O loops handle partial sends/receives and use monotonic poll deadlines.
- Proxy errors: malformed/invalid framing `400`, unsupported method `405` with `Allow: GET`, unsupported HTTP version `505`, request line over 8 KiB `414`, headers over 16 KiB `431`, GET body framing `413`, incomplete headers `408`, and unavailable/invalid upstream `502`. Errors have fixed text bodies and close the connection. `503` requires later health-aware selection; it is not emitted by this fixed-upstream slice. Capacity closes are not yet implemented because the listener is single-threaded.
- Upstream responses are buffered before forwarding, with a 16 KiB header and 8 MiB response bound. A final HTTP/1.0 or HTTP/1.1 status 200–599 with one Content-Length or close-delimited body is accepted; transfer encoding and invalid framing yield `502`. The connection closes after the response.
- This milestone does not yet implement safe end-to-end request-header forwarding, response hop-by-hop header filtering, concurrent worker limits, round-robin/least-connections, health probes/failover/recovery, metrics, the admin API, benchmark client, or dashboard. Those remain required by the main specification.

## Build

From the repository root on macOS with Xcode Command Line Tools and CMake available:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To promote the configured warning set to errors, configure with `-DEDGEMESH_ENABLE_WARNINGS_AS_ERRORS=ON`.

## Run the demonstration

In separate terminals from the repository root:

```sh
./build/edgemesh-backend --port 9001 --id backend-1
./build/edgemesh-proxy
curl --include http://127.0.0.1:8080/demo
```

The backend and proxy CLI accept only numeric port options; the proxy backend host is not configurable. For isolated integration tests, the proxy also accepts `--listen-port PORT`; both listeners still bind only to IPv4 loopback.

Example alternate backend:

```sh
./build/edgemesh-backend --port 9002 --id backend-2
./build/edgemesh-proxy --backend-port 9002
```
