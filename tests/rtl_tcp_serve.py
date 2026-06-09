#!/usr/bin/env python3
"""Minimal fake rtl_tcp server for black-box testing rtl_433's live input path.

Speaks just enough of the rtl_tcp protocol to feed rtl_433 a synthetic OOK
signal over TCP. This drives rtl_433 through its live SDR/poll loop -- which
keeps the HTTP/WS server responsive -- instead of the one-shot `-r`/`-y` file
path that exit()s as soon as the input is consumed (rtl_433.c, the test-data
block that ends in exit(0), runs *before* the mg_mgr_poll loop). That makes it
possible to feed a decode and then read it back over the HTTP API while the
server is still up.

rtl_tcp wire protocol (only the parts rtl_433 needs):
  server -> client: 12-byte header  b"RTL0" + uint32be tuner_type + uint32be gain_count
  client -> server: 5-byte commands (1 byte cmd + uint32be param) -- drained and ignored
  server -> client: raw CU8 IQ sample stream (interleaved unsigned 8-bit I,Q)

The signal is synthesized as OOK_PULSE_PWM to match the rtl_433 pulse slicer:
short pulse = bit 1, long pulse = bit 0, an optional trailing sync pulse to
close the row, framed by a lead-in gap (long enough to settle the detector's
noise estimate) and a final reset gap. After the burst it streams silence so the
TCP connection -- and therefore rtl_433's server -- stays up until the client
goes away or we are signalled.

Pure standard library so it runs in CI without extra packages.

Usage: rtl_tcp_serve.py --port N --bits BITSTRING [options]
  --port N         TCP port to listen on (required; use the same one in -d rtl_tcp:...)
  --bits S         data bits as a string of '0'/'1' (short=1, long=0)
  --rate HZ        sample rate the signal is generated for (default 250000)
  --short US       short pulse width  (default 500)
  --long US        long pulse width   (default 1000)
  --sync US        trailing sync pulse width, 0 to omit (default 1500)
  --gap US         inter-pulse gap    (default 500)
  --lead-in US     leading silence    (default 8000; must exceed ~1024 samples)
  --reset US       trailing silence   (default 6000; must exceed the reset_limit)
Prints "LISTENING <port>" to stdout once bound, so a caller can synchronize.
"""
import argparse
import math
import socket
import struct
import sys
import threading


def synth_cu8(bits, rate, short_us, long_us, sync_us, gap_us, lead_in_us, reset_us):
    """Return CU8 bytes for an OOK_PULSE_PWM burst (short=1, long=0)."""
    samples_per_us = rate / 1e6
    tone_hz = 50000.0  # IF offset so "on" is a real tone, not pure DC
    buf = bytearray()
    phase = [0.0]

    def emit(us, on):
        for _ in range(int(round(us * samples_per_us))):
            if on:
                i = 128 + int(100 * math.cos(phase[0]))
                q = 128 + int(100 * math.sin(phase[0]))
                phase[0] += 2 * math.pi * tone_hz / rate
            else:
                i = q = 128
            buf.append(max(0, min(255, i)))
            buf.append(max(0, min(255, q)))

    emit(lead_in_us, False)
    for b in bits:
        emit(short_us if b == "1" else long_us, True)
        emit(gap_us, False)
    if sync_us > 0:
        emit(sync_us, True)  # closes the data row -> trailing empty row
    emit(reset_us, False)
    return bytes(buf)


def drain_commands(conn):
    """Read and discard the 5-byte rtl_tcp commands rtl_433 sends (set freq,
    rate, gain, ...). We honor none of them; the signal is pre-synthesized."""
    try:
        while True:
            if not conn.recv(4096):
                return
    except OSError:
        return


def serve(port, payload):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(1)
    bound = srv.getsockname()[1]
    print("LISTENING %d" % bound, flush=True)

    conn, _ = srv.accept()
    srv.close()
    try:
        # rtl_tcp dongle-info header: magic + tuner type (5 = R820T) + gain count.
        conn.sendall(b"RTL0" + struct.pack(">II", 5, 0))
        threading.Thread(target=drain_commands, args=(conn,), daemon=True).start()

        conn.sendall(payload)
        # Keep the stream (and thus rtl_433's server) alive with silence until
        # the client disconnects or we're killed.
        silence = bytes([128]) * 16384
        while True:
            conn.sendall(silence)
    except OSError:
        pass  # client went away -- normal shutdown
    finally:
        try:
            conn.close()
        except OSError:
            pass


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, required=True)
    p.add_argument("--bits", required=True)
    p.add_argument("--rate", type=int, default=250000)
    p.add_argument("--short", type=float, default=500)
    p.add_argument("--long", type=float, default=1000)
    p.add_argument("--sync", type=float, default=1500)
    p.add_argument("--gap", type=float, default=500)
    p.add_argument("--lead-in", type=float, default=8000)
    p.add_argument("--reset", type=float, default=6000)
    a = p.parse_args()
    if any(c not in "01" for c in a.bits):
        print("error: --bits must be a string of 0/1", file=sys.stderr)
        return 2
    payload = synth_cu8(a.bits, a.rate, a.short, a.long, a.sync,
                        a.gap, getattr(a, "lead_in"), a.reset)
    serve(a.port, payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
