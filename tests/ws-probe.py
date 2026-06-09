#!/usr/bin/env python3
"""Minimal WebSocket probe for the rtl_433 HTTP/WS API.

Connects to a running rtl_433 HTTP server (started elsewhere, e.g. with
`-D manual -F http://host:port`), completes the WebSocket handshake, reads the
meta frame the server pushes on connect, then sends a JSON-RPC command frame
and reads the reply. This exercises the server's WebSocket code paths
(handshake -> meta/history push -> handle_ws_rpc -> rpc_response_ws).

Pure standard library so it runs in CI without extra packages.

Usage: ws-probe.py HOST PORT [timeout_seconds] [expect_substring]
If expect_substring is given, the probe also asserts that a pushed frame (the
history the server replays on connect, or a live broadcast) contains it -- used
to confirm a decoded event is retrievable over the WS API.
Exit 0 on success, non-zero otherwise; received text frames go to stdout.
"""
import base64
import json
import os
import socket
import sys

OP_TEXT = 0x1
OP_CLOSE = 0x8
OP_PING = 0x9


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("connection closed by server")
        buf += chunk
    return buf


def read_frame(sock):
    """Read one WebSocket frame, return (opcode, payload_bytes)."""
    b0, b1 = recv_exact(sock, 2)
    opcode = b0 & 0x0F
    masked = b1 & 0x80
    length = b1 & 0x7F
    if length == 126:
        length = int.from_bytes(recv_exact(sock, 2), "big")
    elif length == 127:
        length = int.from_bytes(recv_exact(sock, 8), "big")
    mask = recv_exact(sock, 4) if masked else b""
    payload = recv_exact(sock, length) if length else b""
    if masked:
        payload = bytes(p ^ mask[i % 4] for i, p in enumerate(payload))
    return opcode, payload


def send_text(sock, text):
    """Send a masked text frame (clients MUST mask, per RFC 6455)."""
    data = text.encode("utf-8")
    n = len(data)
    header = bytes([0x80 | OP_TEXT])  # FIN + text
    if n < 126:
        header += bytes([0x80 | n])
    elif n < 65536:
        header += bytes([0x80 | 126]) + n.to_bytes(2, "big")
    else:
        header += bytes([0x80 | 127]) + n.to_bytes(8, "big")
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    sock.sendall(header + mask + masked)


def handshake(sock, host, port):
    key = base64.b64encode(os.urandom(16)).decode()
    req = (
        "GET / HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    sock.sendall(req.encode())
    resp = b""
    while b"\r\n\r\n" not in resp:
        resp += recv_exact(sock, 1)
    status = resp.split(b"\r\n", 1)[0].decode(errors="replace")
    if "101" not in status:
        raise RuntimeError(f"handshake failed: {status!r}")


def read_text_frame(sock):
    """Read frames until a text frame arrives, answering pings, ignoring close."""
    while True:
        opcode, payload = read_frame(sock)
        if opcode == OP_TEXT:
            return payload.decode("utf-8", errors="replace")
        if opcode == OP_PING:
            continue  # server ping; ignore for this short-lived probe
        if opcode == OP_CLOSE:
            raise EOFError("server sent close frame")


def read_until_contains(sock, needle, max_frames=20):
    """Read text frames until one contains needle. The server also broadcasts
    log messages over the same socket, so skip frames that aren't the one we
    want. Returns the matching frame or None if max_frames is exhausted."""
    for _ in range(max_frames):
        frame = read_text_frame(sock)
        print("  <<", frame)
        if needle in frame:
            return frame
    return None


def main():
    if len(sys.argv) < 3:
        print("usage: ws-probe.py HOST PORT [timeout]", file=sys.stderr)
        return 2
    host = sys.argv[1]
    port = int(sys.argv[2])
    timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
    expect = sys.argv[4] if len(sys.argv) > 4 else None

    sock = socket.create_connection((host, port), timeout=timeout)
    sock.settimeout(timeout)
    try:
        handshake(sock, host, port)

        # On connect the server pushes the meta frame (and history, empty here).
        if read_until_contains(sock, "center_frequency") is None:
            print("FAIL: never received a meta frame with 'center_frequency'", file=sys.stderr)
            return 1

        # If asked, confirm a decoded event is retrievable over the WS API: the
        # server replays its history ring on connect, so a decode that already
        # happened arrives as a pushed frame (a live broadcast also matches).
        if expect is not None:
            if read_until_contains(sock, expect, max_frames=200) is None:
                print("FAIL: never received a frame containing %r" % expect, file=sys.stderr)
                return 1

        # The WebSocket RPC uses the simple {"cmd": ...} form (json_parse),
        # NOT the JSON-RPC envelope the /jsonrpc HTTP endpoint expects.
        send_text(sock, json.dumps({"cmd": "get_sample_rate"}))
        reply = read_until_contains(sock, "\"result\"")
        if reply is None:
            print("FAIL: no rpc reply containing 'result'", file=sys.stderr)
            return 1

        print("OK: websocket handshake, meta push, and rpc round-trip")
        return 0
    finally:
        try:
            sock.close()
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
