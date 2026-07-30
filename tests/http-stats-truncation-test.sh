#!/bin/sh
#
# Regression test for the HTTP/WS "states" broadcast truncating a large stats
# report into invalid JSON (print_http_data() in src/http_server.c).
#
# print_http_data() serializes every non-"model" data_t (e.g. the get_stats-
# shaped report pushed on SIGINFO/level-3 stats) into a fixed-size buffer.
# With ~335 decoders enabled by default, that report is comfortably larger
# than the historical 20000-byte buffer, so a fixed buffer truncates it mid-
# object -- the same abuf corruption pattern as the get_stats/get_protocols
# RPC bug (see data-test.c's test_jsons_large_report). data_print_jsons_dup()
# grows the buffer to fit instead.
#
# Like http-rtltcp-test.sh, this needs the *live* input path: process_sdr_frame()
# (where the periodic stats check lives) only runs while IQ frames are flowing,
# so a fake rtl_tcp source streams silence to keep rtl_433 in its live poll
# loop. All default decoders stay enabled (no -R restriction) so the stats
# report is big enough to exercise the bug; SIGINFO (signal 29) forces an
# immediate level-3 report instead of waiting on the stats interval timer.
#
# Usage: http-stats-truncation-test.sh [path-to-rtl_433-binary]

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RTL_433=${1:-"$SCRIPT_DIR/../src/rtl_433"}
HOST=127.0.0.1

if [ ! -x "$RTL_433" ]; then
    echo "ERROR: rtl_433 binary not found or not executable: $RTL_433" >&2
    exit 99
fi

pick_port() {
    python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()' 2>/dev/null
}
HTTPPORT=${HTTPPORT:-$(pick_port)}
TCPPORT=${TCPPORT:-$(pick_port)}
HTTPPORT=${HTTPPORT:-18433}
TCPPORT=${TCPPORT:-18533}
BASE="http://$HOST:$HTTPPORT"

SERVER_LOG=$(mktemp 2>/dev/null || echo /tmp/rtl_433_stats_test.$$.log)
TCP_LOG=$(mktemp 2>/dev/null || echo /tmp/rtl_433_stats_srv.$$.log)
RTL_PID=""
TCP_PID=""

cleanup() {
    if [ -n "$RTL_PID" ] && kill -0 "$RTL_PID" 2>/dev/null; then
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

# --- start the fake rtl_tcp server (content is irrelevant; we only need a
# continuous IQ stream to keep rtl_433's live poll loop -- and its periodic
# stats-timer check -- running) ---
echo "Starting fake rtl_tcp server on $HOST:$TCPPORT"
python3 "$SCRIPT_DIR/rtl_tcp_serve.py" --port "$TCPPORT" --bits "0" \
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

# --- start rtl_433 against it, all default decoders enabled (no -R) ---
echo "Starting rtl_433: -d rtl_tcp:$HOST:$TCPPORT -F $BASE"
"$RTL_433" -d "rtl_tcp:$HOST:$TCPPORT" -F "$BASE" >"$SERVER_LOG" 2>&1 &
RTL_PID=$!

ready=0
for _ in $(seq 1 50); do
    if ! kill -0 "$RTL_PID" 2>/dev/null; then
        echo "ERROR: rtl_433 exited during startup" >&2
        cat "$SERVER_LOG" >&2
        exit 95
    fi
    if curl -s -o /dev/null --max-time 1 "$BASE/"; then
        ready=1
        break
    fi
    sleep 0.2
done
if [ "$ready" != 1 ]; then
    echo "ERROR: HTTP server did not become ready on $BASE" >&2
    cat "$SERVER_LOG" >&2
    exit 94
fi
echo "HTTP server ready on $BASE (rtl_433 pid $RTL_PID)"

# --- the actual assertion: SIGINFO forces a full stats report over the WS
# API, and it must arrive as complete, valid JSON, not truncated ---
echo "--- ws stats read-back ---"
if python3 "$SCRIPT_DIR/ws-probe.py" "$HOST" "$HTTPPORT" 10 '"stats":[' "$RTL_PID" 29; then
    echo "ok: full stats report round-tripped through the HTTP/WS API as valid JSON"
    exit 0
else
    echo "NOT OK: stats report did not come back as valid JSON" >&2
    cat "$SERVER_LOG" >&2
    exit 1
fi
