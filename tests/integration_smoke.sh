#!/usr/bin/env bash
# tests/integration_smoke.sh
# EdgeMesh-C end-to-end integration smoke test.
# Covers INT-01 through INT-10 from docs/test-plan.md.
#
# Usage: bash tests/integration_smoke.sh
# Run from the CMake binary directory (CTest sets WORKING_DIRECTORY to build/).
# The script resolves binary paths relative to itself (CMAKE_SOURCE_DIR) and
# the working directory (CMAKE_BINARY_DIR).
#
# Exit code: 0 = all checks passed, 1 = at least one check failed.

set -euo pipefail

# ---------------------------------------------------------------------------
# Resolve paths
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# When invoked by CTest, CWD is build/; the script itself lives in tests/.
# Binaries are in the CWD (build/).
BIN_DIR="${PWD}"
EDGEMESH="${BIN_DIR}/edgemesh"
DEMO_BACKEND="${BIN_DIR}/demo_backend"

# Verify binaries exist
if [[ ! -x "${EDGEMESH}" ]]; then
    echo "FATAL: edgemesh binary not found at ${EDGEMESH}" >&2
    exit 1
fi
if [[ ! -x "${DEMO_BACKEND}" ]]; then
    echo "FATAL: demo_backend binary not found at ${DEMO_BACKEND}" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Globals
# ---------------------------------------------------------------------------
PASS_COUNT=0
FAIL_COUNT=0
BACKEND_PIDS=()
EDGEMESH_PID=""

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
pass() {
    local msg="$1"
    echo "  PASS: ${msg}"
    PASS_COUNT=$(( PASS_COUNT + 1 ))
}

fail() {
    local msg="$1"
    echo "  FAIL: ${msg}" >&2
    FAIL_COUNT=$(( FAIL_COUNT + 1 ))
}

check() {
    local label="$1"
    local result="$2"   # 0 = success
    if [[ "${result}" -eq 0 ]]; then
        pass "${label}"
    else
        fail "${label}"
    fi
}

# json_valid: returns 0 if input is valid JSON, 1 otherwise
json_valid() {
    local body="$1"
    if command -v python3 &>/dev/null; then
        echo "${body}" | python3 -m json.tool >/dev/null 2>&1
    elif command -v jq &>/dev/null; then
        echo "${body}" | jq . >/dev/null 2>&1
    else
        # Rudimentary check: must start with { or [
        [[ "${body}" =~ ^\{ ]] || [[ "${body}" =~ ^\[ ]]
    fi
}

# http_get: fetches URL, writes status code to stdout on first line, body on remaining lines
# Usage: http_get <url> [extra curl args...]
http_get() {
    local url="$1"; shift
    curl -s -w "\n__STATUS__%{http_code}" "${url}" "$@"
}

# extract_status: given http_get output, prints the HTTP status code
extract_status() {
    grep -o '__STATUS__[0-9]*' <<< "$1" | grep -o '[0-9]*'
}

# extract_body: given http_get output, prints everything before the status line
extract_body() {
    sed 's/__STATUS__[0-9]*$//' <<< "$1" | head -c 65536
}

# ---------------------------------------------------------------------------
# Cleanup — always kill background processes on exit
# ---------------------------------------------------------------------------
cleanup() {
    echo ""
    echo "--- Cleaning up background processes ---"
    # Kill edgemesh
    if [[ -n "${EDGEMESH_PID}" ]] && kill -0 "${EDGEMESH_PID}" 2>/dev/null; then
        kill "${EDGEMESH_PID}" 2>/dev/null || true
    fi
    # Kill all demo_backend processes
    for pid in "${BACKEND_PIDS[@]:-}"; do
        if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
            kill "${pid}" 2>/dev/null || true
        fi
    done
    # Wait briefly for them to exit
    sleep 1
    echo "--- Cleanup done ---"
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Helper: start a demo_backend and record its PID
# ---------------------------------------------------------------------------
start_backend() {
    local port="$1"
    "${DEMO_BACKEND}" "${port}" &
    BACKEND_PIDS+=("$!")
    echo "  Started demo_backend on port ${port} (PID $!)"
}

# ---------------------------------------------------------------------------
# Helper: stop backend by port (kills the PID stored for it)
# ---------------------------------------------------------------------------
stop_backend_pid() {
    local pid="$1"
    if kill -0 "${pid}" 2>/dev/null; then
        kill "${pid}" 2>/dev/null || true
        echo "  Stopped PID ${pid}"
    fi
}

# ---------------------------------------------------------------------------
echo "========================================================"
echo "EdgeMesh-C Integration Smoke Test"
echo "Binaries: ${EDGEMESH}, ${DEMO_BACKEND}"
echo "========================================================"
echo ""

# ---------------------------------------------------------------------------
# INT-01: Start three demo backends and edgemesh
# ---------------------------------------------------------------------------
echo "[INT-01] Starting backends and edgemesh proxy..."

start_backend 9001
start_backend 9002
start_backend 9003

# Capture PIDs for later selective kill
B1_PID="${BACKEND_PIDS[0]}"
B2_PID="${BACKEND_PIDS[1]}"
B3_PID="${BACKEND_PIDS[2]}"

# Start edgemesh
"${EDGEMESH}" &
EDGEMESH_PID="$!"
echo "  Started edgemesh (PID ${EDGEMESH_PID})"

echo "  Waiting 12s for listeners to bind and initial health probes to complete..."
echo "  (Backends need 2 successful probes x 5s interval = 10s to transition UNKNOWN->HEALTHY)"
sleep 12

# Verify processes are still running
if kill -0 "${EDGEMESH_PID}" 2>/dev/null; then
    pass "INT-01: edgemesh started and running"
else
    fail "INT-01: edgemesh exited prematurely"
fi

# ---------------------------------------------------------------------------
# INT-02: Send GET through proxy, verify 200 + backend identity
# ---------------------------------------------------------------------------
echo ""
echo "[INT-02] Verifying proxy forwards GET requests..."

RAW=$(http_get "http://127.0.0.1:8080/" 2>/dev/null) || true
STATUS=$(extract_status "${RAW}")
BODY=$(extract_body "${RAW}")

if [[ "${STATUS}" == "200" ]] && echo "${BODY}" | grep -q "Backend at port"; then
    pass "INT-02: proxy returned 200 with backend identity"
else
    fail "INT-02: expected 200 + 'Backend at port', got status=${STATUS} body=${BODY}"
fi

# ---------------------------------------------------------------------------
# INT-03: Round-robin distribution over 6 requests
# ---------------------------------------------------------------------------
echo ""
echo "[INT-03] Checking round-robin distribution over 6 requests..."

SEEN_9001=0; SEEN_9002=0; SEEN_9003=0
for i in $(seq 1 6); do
    RAW=$(http_get "http://127.0.0.1:8080/" 2>/dev/null) || true
    STATUS=$(extract_status "${RAW}")
    BODY=$(extract_body "${RAW}")
    if [[ "${STATUS}" != "200" ]]; then
        fail "INT-03: request ${i} returned status ${STATUS}, expected 200"
        continue
    fi
    if echo "${BODY}" | grep -q "port 9001"; then SEEN_9001=$(( SEEN_9001 + 1 )); fi
    if echo "${BODY}" | grep -q "port 9002"; then SEEN_9002=$(( SEEN_9002 + 1 )); fi
    if echo "${BODY}" | grep -q "port 9003"; then SEEN_9003=$(( SEEN_9003 + 1 )); fi
done

echo "  Backend distribution: 9001=${SEEN_9001} 9002=${SEEN_9002} 9003=${SEEN_9003}"

if [[ "${SEEN_9001}" -ge 1 ]] && [[ "${SEEN_9002}" -ge 1 ]] && [[ "${SEEN_9003}" -ge 1 ]]; then
    pass "INT-03: all three backends appeared in 6 requests (round-robin)"
else
    fail "INT-03: not all backends appeared; distribution: 9001=${SEEN_9001} 9002=${SEEN_9002} 9003=${SEEN_9003}"
fi

# ---------------------------------------------------------------------------
# INT-10 (policy): PUT /api/v1/policy — valid value
# ---------------------------------------------------------------------------
echo ""
echo "[INT-10a] Testing PUT /api/v1/policy with valid 'least_connections'..."

RAW=$(curl -s -w "\n__STATUS__%{http_code}" -X PUT \
    -H "Content-Type: application/json" \
    -d '{"policy":"least_connections"}' \
    "http://127.0.0.1:9090/api/v1/policy" 2>/dev/null) || true
STATUS=$(extract_status "${RAW}")
BODY=$(extract_body "${RAW}")

if [[ "${STATUS}" == "200" ]]; then
    pass "INT-10a: PUT least_connections returned 200"
else
    fail "INT-10a: PUT least_connections expected 200, got ${STATUS} (body=${BODY})"
fi

# ---------------------------------------------------------------------------
# INT-10 (policy): PUT /api/v1/policy — invalid value → 400
# ---------------------------------------------------------------------------
echo ""
echo "[INT-10b] Testing PUT /api/v1/policy with invalid 'bad_value'..."

RAW=$(curl -s -w "\n__STATUS__%{http_code}" -X PUT \
    -H "Content-Type: application/json" \
    -d '{"policy":"bad_value"}' \
    "http://127.0.0.1:9090/api/v1/policy" 2>/dev/null) || true
STATUS=$(extract_status "${RAW}")
BODY=$(extract_body "${RAW}")

if [[ "${STATUS}" == "400" ]]; then
    pass "INT-10b: PUT bad_value correctly returned 400"
else
    fail "INT-10b: PUT bad_value expected 400, got ${STATUS} (body=${BODY})"
fi

# Switch back to round_robin for the remainder of the test
curl -s -X PUT \
    -H "Content-Type: application/json" \
    -d '{"policy":"round_robin"}' \
    "http://127.0.0.1:9090/api/v1/policy" >/dev/null 2>&1 || true

# ---------------------------------------------------------------------------
# INT-10 (admin endpoints): all 6 GET endpoints return 200 + valid JSON
# ---------------------------------------------------------------------------
echo ""
echo "[INT-10c] Testing all 6 admin GET endpoints..."

ADMIN_ENDPOINTS=(
    "http://127.0.0.1:9090/api/v1/health"
    "http://127.0.0.1:9090/api/v1/overview"
    "http://127.0.0.1:9090/api/v1/backends"
    "http://127.0.0.1:9090/api/v1/metrics"
    "http://127.0.0.1:9090/api/v1/metrics?window_seconds=60"
    "http://127.0.0.1:9090/api/v1/benchmarks/latest"
)

for endpoint in "${ADMIN_ENDPOINTS[@]}"; do
    RAW=$(http_get "${endpoint}" 2>/dev/null) || true
    STATUS=$(extract_status "${RAW}")
    BODY=$(extract_body "${RAW}")
    label="GET ${endpoint}"

    if [[ "${STATUS}" != "200" ]]; then
        fail "${label}: expected 200, got ${STATUS}"
        continue
    fi

    if json_valid "${BODY}"; then
        pass "${label}: 200 + valid JSON"
    else
        fail "${label}: 200 but invalid JSON (body=${BODY:0:120})"
    fi
done

# ---------------------------------------------------------------------------
# INT-05: Kill one backend, wait for health probe to mark it unhealthy
# ---------------------------------------------------------------------------
echo ""
echo "[INT-05] Stopping backend on 9001 and waiting for unhealthy state..."
echo "         (3 failures × 5s probe interval + 3s buffer = 18s)"

stop_backend_pid "${B1_PID}"
sleep 18

# Check backends endpoint for unhealthy
RAW=$(http_get "http://127.0.0.1:9090/api/v1/backends" 2>/dev/null) || true
STATUS=$(extract_status "${RAW}")
BODY=$(extract_body "${RAW}")

if [[ "${STATUS}" == "200" ]] && echo "${BODY}" | grep -q '"unhealthy"'; then
    pass "INT-05: backend correctly marked unhealthy after stop"
else
    fail "INT-05: expected unhealthy state, got status=${STATUS} body=${BODY:0:200}"
fi

# ---------------------------------------------------------------------------
# INT-05 (continued): remaining backends still serve requests
# ---------------------------------------------------------------------------
echo ""
echo "[INT-05b] Verifying remaining backends still serve requests..."

RAW=$(http_get "http://127.0.0.1:8080/" 2>/dev/null) || true
STATUS=$(extract_status "${RAW}")
BODY=$(extract_body "${RAW}")

if [[ "${STATUS}" == "200" ]] && echo "${BODY}" | grep -q "Backend at port"; then
    pass "INT-05b: remaining backends serving (not 503)"
else
    fail "INT-05b: expected 200 from remaining backends, got ${STATUS} body=${BODY}"
fi

# ---------------------------------------------------------------------------
# INT-07: Kill all backends, verify 503
# ---------------------------------------------------------------------------
echo ""
echo "[INT-07] Stopping all backends, verifying 503 from proxy..."

stop_backend_pid "${B2_PID}"
stop_backend_pid "${B3_PID}"
# Wait for health checks to mark remaining backends unhealthy
# 9002 and 9003 were healthy; need 3 failures × 5s = 15s + buffer
echo "         Waiting 18s for all backends to go unhealthy..."
sleep 18

RAW=$(http_get "http://127.0.0.1:8080/" 2>/dev/null) || true
STATUS=$(extract_status "${RAW}")

if [[ "${STATUS}" == "503" ]]; then
    pass "INT-07: proxy correctly returns 503 when all backends unhealthy"
else
    fail "INT-07: expected 503, got ${STATUS}"
fi

# ---------------------------------------------------------------------------
# INT-06: Restart backends, verify recovery
# ---------------------------------------------------------------------------
echo ""
echo "[INT-06] Restarting all backends and waiting for recovery..."
echo "         (2 successes × 5s probe interval + 2s buffer = 12s)"

# Reset the BACKEND_PIDS array and restart
BACKEND_PIDS=()
start_backend 9001
start_backend 9002
start_backend 9003
B1_PID="${BACKEND_PIDS[0]}"
B2_PID="${BACKEND_PIDS[1]}"
B3_PID="${BACKEND_PIDS[2]}"

sleep 12

RAW=$(http_get "http://127.0.0.1:8080/" 2>/dev/null) || true
STATUS=$(extract_status "${RAW}")
BODY=$(extract_body "${RAW}")

if [[ "${STATUS}" == "200" ]] && echo "${BODY}" | grep -q "Backend at port"; then
    pass "INT-06: backends recovered — proxy serving again after restart"
else
    fail "INT-06: expected 200 after recovery, got ${STATUS} body=${BODY}"
fi

# Also verify the backends endpoint shows at least one healthy
RAW=$(http_get "http://127.0.0.1:9090/api/v1/backends" 2>/dev/null) || true
STATUS=$(extract_status "${RAW}")
BODY=$(extract_body "${RAW}")

if [[ "${STATUS}" == "200" ]] && echo "${BODY}" | grep -q '"healthy"'; then
    pass "INT-06b: backends/latest shows at least one healthy backend"
else
    fail "INT-06b: expected healthy state after recovery, got status=${STATUS} body=${BODY:0:200}"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "========================================================"
echo "Results: ${PASS_COUNT} passed, ${FAIL_COUNT} failed"
echo "========================================================"

if [[ "${FAIL_COUNT}" -eq 0 ]]; then
    echo "ALL CHECKS PASSED"
    exit 0
else
    echo "SOME CHECKS FAILED" >&2
    exit 1
fi
