#!/bin/sh
#
# Black-box integration test for the rtl_433 HTTP/WS API server.
#
# Starts rtl_433 in "manual" device mode (-D manual), which runs the HTTP
# server event loop without any SDR hardware or input file, exercises each
# endpoint over the loopback interface, then shuts the server down with
# SIGTERM so it exits cleanly (this is what lets gcov flush .gcda files when
# the binary was built with -DENABLE_COVERAGE=ON).
#
# Usage: http-integration-test.sh [path-to-rtl_433-binary]
#   Defaults to ../src/rtl_433 relative to this script.
#   Override the port with PORT=NNNN, otherwise a free port is chosen.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RTL_433=${1:-"$SCRIPT_DIR/../src/rtl_433"}
HOST=127.0.0.1

if [ ! -x "$RTL_433" ]; then
    echo "ERROR: rtl_433 binary not found or not executable: $RTL_433" >&2
    exit 99
fi

# Pick a free TCP port unless one was given, so parallel ctest runs don't collide.
if [ -z "${PORT:-}" ]; then
    PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()' 2>/dev/null)
fi
PORT=${PORT:-18433}
BASE="http://$HOST:$PORT"

SERVER_LOG=$(mktemp 2>/dev/null || echo /tmp/rtl_433_http_test.$$.log)
SERVER_PID=""
FAILED=0
PASSED=0

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        # SIGTERM -> clean shutdown -> atexit/gcov flush. Do NOT use SIGKILL.
        kill -TERM "$SERVER_PID" 2>/dev/null
        # give it a moment to drain and write coverage data
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            kill -0 "$SERVER_PID" 2>/dev/null || break
            sleep 0.3
        done
        kill -0 "$SERVER_PID" 2>/dev/null && kill -KILL "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    rm -f "$SERVER_LOG"
}
trap cleanup EXIT INT TERM

fail() { echo "  NOT OK: $1" >&2; FAILED=$((FAILED + 1)); }
pass() { echo "  ok: $1"; PASSED=$((PASSED + 1)); }

# expect_status DESC EXPECTED_CODE curl-args...
expect_status() {
    desc=$1; want=$2; shift 2
    got=$(curl -s -o /dev/null -w '%{http_code}' --max-time 10 "$@")
    [ "$got" = "$want" ] && pass "$desc (HTTP $got)" || fail "$desc: expected HTTP $want, got $got"
}

# expect_body_contains DESC SUBSTRING curl-args...
expect_body_contains() {
    desc=$1; needle=$2; shift 2
    body=$(curl -s --max-time 10 "$@")
    case "$body" in
        *"$needle"*) pass "$desc (body contains '$needle')" ;;
        *) fail "$desc: body did not contain '$needle'"; printf '    got: %.200s\n' "$body" >&2 ;;
    esac
}

# expect_valid_json DESC curl-args... -- body must parse as JSON. Guards against
# the getters truncating an oversized report into invalid JSON (see get_stats /
# get_protocols, which now grow their output buffer to fit). Skipped when
# python3 is unavailable to validate.
HAVE_PY=0
command -v python3 >/dev/null 2>&1 && HAVE_PY=1
expect_valid_json() {
    desc=$1; shift
    if [ "$HAVE_PY" != 1 ]; then
        echo "  skip: $desc (python3 not found)"
        return
    fi
    body=$(curl -s --max-time 10 "$@")
    if printf '%s' "$body" | python3 -c 'import sys,json; json.load(sys.stdin)' 2>/dev/null; then
        pass "$desc (valid JSON)"
    else
        fail "$desc: response is not valid JSON"; printf '    got: %.200s\n' "$body" >&2
    fi
}

# expect_stream DESC URL -- streaming endpoints stay open; we only check the
# response headers, tolerating the curl timeout (exit 28) that follows.
expect_stream() {
    desc=$1; url=$2
    hdr=$(curl -s -D - -o /dev/null --max-time 2 "$url" 2>/dev/null)
    case "$hdr" in
        *"200 OK"*) pass "$desc (200, stream open)" ;;
        *) fail "$desc: no '200 OK' in response headers"; printf '    got: %.200s\n' "$hdr" >&2 ;;
    esac
}

echo "Starting rtl_433 HTTP server: $RTL_433 -D manual -F $BASE"
"$RTL_433" -D manual -F "http://$HOST:$PORT" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!

# Wait for readiness (server prints/serves once the manual loop is up).
ready=0
for _ in $(seq 1 50); do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "ERROR: server exited during startup" >&2
        cat "$SERVER_LOG" >&2
        exit 98
    fi
    if curl -s -o /dev/null --max-time 1 "$BASE/"; then
        ready=1
        break
    fi
    sleep 0.2
done
if [ "$ready" != 1 ]; then
    echo "ERROR: server did not become ready on $BASE" >&2
    cat "$SERVER_LOG" >&2
    exit 97
fi

echo "Server ready on port $PORT (pid $SERVER_PID). Running checks:"

# --- static / UI ---
expect_status        "GET /  returns 200"          200 "$BASE/"
expect_body_contains "GET /  serves index html"    "DOCTYPE html" "$BASE/"
expect_status        "OPTIONS /  CORS preflight"   204 -X OPTIONS "$BASE/"
expect_status        "GET /ui  redirects"          307 "$BASE/ui"

# --- OpenMetrics / Prometheus ---
expect_status        "GET /metrics returns 200"    200 "$BASE/metrics"
expect_body_contains "GET /metrics is OpenMetrics" "# TYPE uptime_seconds counter" "$BASE/metrics"
# Non-GET is rejected; send an explicit (empty) body so mongoose frames the
# request rather than waiting for a body that curl's bare -X POST never sends.
expect_status        "POST /metrics is rejected"   405 -d '' "$BASE/metrics"

# --- /cmd RPC (GET query string and POST form) ---
expect_body_contains "GET /cmd?cmd=get_meta"       "\"samp_rate\"" "$BASE/cmd?cmd=get_meta"
expect_status        "GET /cmd?cmd=get_sample_rate" 200 "$BASE/cmd?cmd=get_sample_rate"
expect_body_contains "POST /cmd form get_meta"     "\"samp_rate\"" -d "cmd=get_meta" "$BASE/cmd"
expect_status        "GET /cmd with no cmd"        200 "$BASE/cmd"

# --- more RPC methods, to exercise the rpc_exec getter/setter branches ---
expect_body_contains "GET /cmd get_protocols"      "Nice" "$BASE/cmd?cmd=get_protocols"
expect_status        "GET /cmd get_stats"          200 "$BASE/cmd?cmd=get_stats"
# These JSON-payload getters build large reports; verify the whole document is
# well-formed (get_protocols is ~100k, well past the buffer's initial size).
expect_valid_json    "GET /cmd get_protocols is valid JSON" "$BASE/cmd?cmd=get_protocols"
expect_valid_json    "GET /cmd get_stats is valid JSON"     "$BASE/cmd?cmd=get_stats"
expect_valid_json    "GET /cmd get_meta is valid JSON"      "$BASE/cmd?cmd=get_meta"
expect_status        "GET /cmd get_center_frequency" 200 "$BASE/cmd?cmd=get_center_frequency"
expect_status        "GET /cmd setter center_frequency" 200 "$BASE/cmd?cmd=center_frequency&val=433920000"
expect_body_contains "GET /cmd unknown method is rejected" "Unknown method" "$BASE/cmd?cmd=no_such_method"

# --- JSON-RPC ---
expect_status        "POST /jsonrpc valid method"  200 \
    -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":1,"method":"get_sample_rate"}' "$BASE/jsonrpc"
expect_body_contains "POST /jsonrpc method with params" "result" \
    -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":2,"method":"sample_rate","params":[1024000]}' "$BASE/jsonrpc"
expect_status        "POST /jsonrpc malformed body does not crash" 200 \
    -H 'Content-Type: application/json' -d 'not json at all' "$BASE/jsonrpc"

# --- streaming endpoints ---
expect_stream        "GET /stream opens"           "$BASE/stream"
expect_stream        "GET /events opens"           "$BASE/events"

# --- WebSocket: handshake, meta push, and an RPC round-trip ---
if command -v python3 >/dev/null 2>&1; then
    if ws_out=$(python3 "$SCRIPT_DIR/ws-probe.py" "$HOST" "$PORT" 10 2>&1); then
        pass "websocket handshake + meta + rpc round-trip"
    else
        fail "websocket probe failed"
        printf '%s\n' "$ws_out" | sed 's/^/    /' >&2
    fi
else
    echo "  skip: websocket probe (python3 not found)"
fi

# --- robustness: unknown path must not crash the server ---
curl -s -o /dev/null --max-time 2 "$BASE/no/such/path" 2>/dev/null
if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "server still alive after unknown path"
else
    fail "server died after request to unknown path"
fi

echo
echo "Results: $PASSED passed, $FAILED failed"
if [ "$FAILED" -ne 0 ]; then
    echo "--- server log ---" >&2
    cat "$SERVER_LOG" >&2
    exit 1
fi
exit 0
