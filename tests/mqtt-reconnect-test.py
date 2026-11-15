#!/usr/bin/env python3
"""Black-box test of the MQTT client's handling of an unresponsive broker.

Plays a scripted fake MQTT broker on the loopback interface and runs rtl_433 in
manual device mode (-D manual), which runs the network event loop without any
SDR hardware or input file. Each scenario checks that rtl_433 drops a
connection the broker doesn't accept and reconnects, instead of staying
connected-but-silent forever:

  connack_timeout   broker accepts the connection but never sends CONNACK
                    (like a proxy with no route to the broker): rtl_433 must
                    close it after ~10 s and reconnect
  connack_rejected  broker rejects every CONNECT: rtl_433 must close at once,
                    and the reconnect delay must back off instead of spinning

Pure standard library so it runs in CI without extra packages.

Usage: mqtt-reconnect-test.py path-to-rtl_433 [scenario...]
  Runs all scenarios when none are named.
"""
import signal
import socket
import subprocess
import sys
import time

# MQTT control packet types (high nibble of the fixed header)
CONNECT = 1
CONNACK = 2
PUBLISH = 3

CONNACK_TIMEOUT = 10


class Closed(Exception):
    """The client closed the connection."""


class Broker:
    def __init__(self):
        self.srv = socket.socket()
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", 0))
        self.srv.listen(8)
        self.port = self.srv.getsockname()[1]

    def accept(self, timeout):
        self.srv.settimeout(timeout)
        try:
            conn, _ = self.srv.accept()
        except socket.timeout:
            return None
        return conn

    def close(self):
        self.srv.close()


def recv_exact(conn, n, deadline):
    buf = b""
    while len(buf) < n:
        conn.settimeout(max(0.01, deadline - time.monotonic()))
        try:
            chunk = conn.recv(n - len(buf))
        except socket.timeout:
            return None
        except ConnectionResetError:
            raise Closed()
        if not chunk:
            raise Closed()
        buf += chunk
    return buf


def read_packet(conn, timeout):
    """Return (type, body), None on timeout, or raise Closed."""
    deadline = time.monotonic() + timeout
    hdr = recv_exact(conn, 1, deadline)
    if hdr is None:
        return None
    length, shift = 0, 0
    while True:
        b = recv_exact(conn, 1, deadline + 5)
        if b is None:
            return None
        length |= (b[0] & 0x7F) << shift
        shift += 7
        if not b[0] & 0x80:
            break
    body = recv_exact(conn, length, deadline + 5) if length else b""
    return hdr[0] >> 4, body


def wait_for(conn, ptype, timeout, ignore=(PUBLISH,)):
    """Read packets until one of ptype; ignore=PUBLISH skips QoS 0 publishes.
    Returns the packet time, None on timeout, or raises Closed."""
    deadline = time.monotonic() + timeout
    while True:
        pkt = read_packet(conn, deadline - time.monotonic())
        if pkt is None:
            return None
        if pkt[0] == ptype:
            return time.monotonic()
        if pkt[0] not in ignore:
            raise AssertionError("unexpected packet type %d" % pkt[0])


def wait_closed(conn, timeout):
    """Return the time the client closed conn, or None if still open."""
    deadline = time.monotonic() + timeout
    try:
        while time.monotonic() < deadline:
            if read_packet(conn, deadline - time.monotonic()) is None:
                return None
    except Closed:
        return time.monotonic()
    return None


def start_rtl_433(rtl_433, port):
    return subprocess.Popen(
        [rtl_433, "-D", "manual", "-F", "mqtt://127.0.0.1:%d" % port, "-F", "log"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )


def stop_rtl_433(proc):
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    return proc.stderr.read().decode(errors="replace")


class Scenario:
    def __init__(self, name):
        self.name = name
        self.failed = 0

    def check(self, cond, desc):
        if cond:
            print("  ok: %s" % desc)
        else:
            print("  NOT OK: %s" % desc, file=sys.stderr)
            self.failed += 1
        return cond


def connect(broker, s, desc):
    conn = broker.accept(15)
    if not s.check(conn is not None, "%s: rtl_433 connects" % desc):
        return None, None
    t = wait_for(conn, CONNECT, 5, ignore=())
    if not s.check(t is not None, "%s: sends CONNECT" % desc):
        conn.close()
        return None, None
    return conn, t


def connack_timeout(broker, s):
    conn, t_connect = connect(broker, s, "first connection")
    if not conn:
        return
    t_closed = wait_closed(conn, CONNACK_TIMEOUT + 10)
    conn.close()
    if not s.check(t_closed is not None, "closes a connection with no CONNACK"):
        return
    waited = t_closed - t_connect
    s.check(
        CONNACK_TIMEOUT - 1 <= waited <= CONNACK_TIMEOUT + 3,
        "closes after ~%d s (took %.1f s)" % (CONNACK_TIMEOUT, waited),
    )
    conn, _ = connect(broker, s, "reconnect")
    if conn:
        conn.close()


def connack_rejected(broker, s):
    connects = []
    for i in range(5):
        conn, t_connect = connect(broker, s, "attempt %d" % (i + 1))
        if not conn:
            return
        connects.append(t_connect)
        conn.sendall(bytes([CONNACK << 4, 2, 0, 5]))  # 5: not authorized
        t_closed = wait_closed(conn, 5)
        conn.close()
        if not s.check(
            t_closed is not None and t_closed - t_connect < 2,
            "attempt %d: closes the connection on a rejected CONNACK" % (i + 1),
        ):
            return
    # reconnect delays back off 0, 1, 3, 6 s rather than staying at 0
    gaps = [b - a for a, b in zip(connects, connects[1:])]
    s.check(
        gaps[-1] >= 4 and gaps[-1] > gaps[0] + 3,
        "reconnect delay backs off (gaps %s s)" % ", ".join("%.1f" % g for g in gaps),
    )


SCENARIOS = {
    "connack_timeout": connack_timeout,
    "connack_rejected": connack_rejected,
}


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 99
    rtl_433 = sys.argv[1]
    names = sys.argv[2:] or list(SCENARIOS)
    failed = 0
    for name in names:
        if name not in SCENARIOS:
            print("unknown scenario: %s" % name, file=sys.stderr)
            return 99
        print("# %s" % name)
        s = Scenario(name)
        broker = Broker()
        proc = start_rtl_433(rtl_433, broker.port)
        try:
            SCENARIOS[name](broker, s)
        finally:
            log = stop_rtl_433(proc)
            broker.close()
        if s.failed:
            print("  rtl_433 output:\n" + log, file=sys.stderr)
        failed += s.failed
    print("%s" % ("FAILED" if failed else "PASSED"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
