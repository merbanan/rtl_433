#!/bin/sh
#
# End-to-end read-back test of the rtl_433 HTTP/WS API over the *live* input
# path.
#
# http-integration-test.sh can't do this: it serves an idle server (no decodes).
# The one-shot -y/-r file path can't either -- it exit()s as soon as the input
# is consumed (before the mg_mgr_poll loop), so no client could connect in time.
# Only the live SDR/poll loop keeps the server up while a decode flows, which is
# what the fake rtl_tcp source below gives us.
#
# Here a fake rtl_tcp server (tests/rtl_tcp_serve.py) streams a synthetic OOK
# signal, so rtl_433 runs through its live SDR/poll loop and the HTTP server
# stays up. We feed a known Nice Flor-s vector, wait for the decode, then connect
# a WebSocket client and assert the event comes back in the server's history
# replay -- a genuine event -> history -> broadcast -> wire round-trip.
#
# Usage: http-rtltcp-test.sh [path-to-rtl_433-binary]

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RTL_433=${1:-"$SCRIPT_DIR/../src/rtl_433"}
HOST=127.0.0.1

if [ ! -x "$RTL_433" ]; then
    echo "ERROR: rtl_433 binary not found or not executable: $RTL_433" >&2
    exit 99
fi

# A documented Nice Flor-s test vector (see src/devices/nice_flor_s.c). The
# bitstring is 0xe7a760b94372e (52 bits) as the pulse slicer sees it;
# rtl_tcp_serve.py renders short=1 / long=0 OOK pulses.
DEVICE_NAME="Nice Flor-s remote control"
BITS="1110011110100111011000001011100101000011011100101110"
EXPECT_MODEL="Nice-FlorS"

PROTO=$("$RTL_433" -R help 2>&1 \
    | grep "$DEVICE_NAME" \
    | grep -oE '\[[0-9]+\]' | head -1 | tr -d '[]')
if [ -z "$PROTO" ]; then
    echo "ERROR: could not find protocol number for '$DEVICE_NAME'" >&2
    exit 98
fi
echo "Resolved '$DEVICE_NAME' to protocol -R $PROTO"

# Pick two free ports (HTTP API + fake rtl_tcp) so parallel ctest runs don't collide.
pick_port() {
    python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()' 2>/dev/null
}
HTTPPORT=${HTTPPORT:-$(pick_port)}
TCPPORT=${TCPPORT:-$(pick_port)}
HTTPPORT=${HTTPPORT:-18433}
TCPPORT=${TCPPORT:-18533}
BASE="http://$HOST:$HTTPPORT"

SERVER_LOG=$(mktemp 2>/dev/null || echo /tmp/rtl_433_rtltcp_test.$$.log)
TCP_LOG=$(mktemp 2>/dev/null || echo /tmp/rtl_433_rtltcp_srv.$$.log)
RTL_PID=""
TCP_PID=""

cleanup() {
    if [ -n "$RTL_PID" ] && kill -0 "$RTL_PID" 2>/dev/null; then
        # SIGTERM -> clean shutdown -> atexit/gcov flush. Do NOT use SIGKILL.
        kill -TERM "$RTL_PID" 2>/dev/null
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            kill -0 "$RTL_PID" 2>/dev/null || break
            sleep 0.3
        done
        kill -0 "$RTL_PID" 2>/dev/null && kill -KILL "$RTL_PID" 2>/dev/null
        wait "$RTL_PID" 2>/dev/null
    fi
    if [ -n "$TCP_PID" ] && kill -0 "$TCP_PID" 2>/dev/null; then
        kill -TERM "$TCP_PID" 2>/dev/null
        wait "$TCP_PID" 2>/dev/null
    fi
    rm -f "$SERVER_LOG" "$TCP_LOG"
}
trap cleanup EXIT INT TERM

# --- start the fake rtl_tcp server and wait until it is listening ---
echo "Starting fake rtl_tcp server on $HOST:$TCPPORT"
python3 "$SCRIPT_DIR/rtl_tcp_serve.py" --port "$TCPPORT" --bits "$BITS" \
    >"$TCP_LOG" 2>&1 &
TCP_PID=$!
listening=0
for _ in $(seq 1 50); do
    if ! kill -0 "$TCP_PID" 2>/dev/null; then
        echo "ERROR: rtl_tcp server exited during startup" >&2
        cat "$TCP_LOG" >&2
        exit 96
    fi
    grep -q "LISTENING" "$TCP_LOG" 2>/dev/null && { listening=1; break; }
    sleep 0.1
done
if [ "$listening" != 1 ]; then
    echo "ERROR: rtl_tcp server did not start listening" >&2
    cat "$TCP_LOG" >&2
    exit 96
fi

# --- start rtl_433 against it, with HTTP API + json so we can observe the decode ---
echo "Starting rtl_433: -d rtl_tcp:$HOST:$TCPPORT -R $PROTO -F $BASE"
"$RTL_433" -d "rtl_tcp:$HOST:$TCPPORT" -R "$PROTO" \
    -F "http://$HOST:$HTTPPORT" -F json >"$SERVER_LOG" 2>&1 &
RTL_PID=$!

# Wait for the HTTP server to come up *and* for the decode to land (the signal
# plays ~0.5 s into the stream). Polling the json output makes the WS read-back
# deterministic: once the event is decoded it is in the history ring for replay.
ready=0
for _ in $(seq 1 100); do
    if ! kill -0 "$RTL_PID" 2>/dev/null; then
        echo "ERROR: rtl_433 exited during startup" >&2
        cat "$SERVER_LOG" >&2
        exit 95
    fi
    if grep -q "$EXPECT_MODEL" "$SERVER_LOG" 2>/dev/null \
            && curl -s -o /dev/null --max-time 1 "$BASE/"; then
        ready=1
        break
    fi
    sleep 0.2
done
if [ "$ready" != 1 ]; then
    echo "ERROR: never saw a '$EXPECT_MODEL' decode with the HTTP server up" >&2
    cat "$SERVER_LOG" >&2
    exit 94
fi
echo "Decode observed and HTTP server ready on $BASE"

# --- the actual assertion: the decode is retrievable over the WS API ---
echo "--- ws read-back ---"
if python3 "$SCRIPT_DIR/ws-probe.py" "$HOST" "$HTTPPORT" 10 "$EXPECT_MODEL"; then
    echo "ok: '$EXPECT_MODEL' decode round-tripped through the HTTP/WS API"
    exit 0
else
    echo "NOT OK: decode did not come back over the WS API" >&2
    cat "$SERVER_LOG" >&2
    exit 1
fi
