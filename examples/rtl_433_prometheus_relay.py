#!/usr/bin/env python3

"""
Prometheus/OpenMetrics relay for rtl_433.

Can either take in JSON from stdin:

rtl_433 ... -F json | examples/rtl_433_prometheus_relay.py

or through UDP syslog packets:

rtl_433 ... -F syslog:0:4433
examples/rtl_433_prometheus_relay.py 4433

Then have Prometheus scrape it on port 30584, or change _SRV_PORT below.

Built for and tested with Oregon Scientific weather sensors, but *should*
work for other kinds (but only for numeric data).
"""

import collections
import json
import os
import socket
import sys
import threading
import time

import dateutil.parser
import http.server

## 0x77 w 0x78 x → port 30584
_SRV_PORT = 0x7778


class rtl_433(object):
    # These fields are used to separate and tag unique sensors, instead of being exported
    # as readings/values.
    _ID_FIELDS = [
        ("model", str),
        ("type", str),
        ("subtype", str),
        ("channel", int),
        ("id", int),
    ]
    _MIN_REPEAT_SECS = 3600  # See a device at least twice in this timespan before sharing it.
    _EXPORT_TIMESTAMPS = True

    _MAX_AGE_SECS = 300  # Used for gc, and max age to show *anything* for given id
    _BACKLOG_SECS = 60   # If multiple samples within this timestamp, share them all

    _LOG_CLEAN_INTERVAL = 1000  # Number of iterations

    log = []  # [(timestamp, id_fields, variable name, value), ...]

    def __init__(self, stream):
        self.stream = stream

    def loop(self):
        n = 0
        last = {}
        for line in self.stream:
            now = time.time()
            print(line.strip())
            try:
                pkt = json.loads(line)
            except json.decoder.JSONDecodeError as e:
                print("Parse error: %s" % e.msg)
                continue
            if not isinstance(pkt, dict):
                print("Not a dict/object: %r" % pkt)
                continue
            try:
                ts = dateutil.parser.isoparse(pkt.pop("time")).timestamp()
            except KeyError:
                ts = now
            if int(ts) == ts:
                # Check above in case we start getting fed non-integer timestamps already.
                # If not, use current precise time instead.
                if abs(now - (ts + 0.5)) < 1:
                    ts = now

            id = self.grab_id(pkt)
            ago = ts - last.get(id, 0)
            last[id] = ts
            if ago < self._MIN_REPEAT_SECS:
                new = []
                for k, v in pkt.items():
                    try:
                        new.append((ts, id, k, float(v)))
                    except (ValueError, TypeError):
                        print("%s{%r} has non-numeric value %s" % (k, id, v))
                        continue
                self.log += new

            n += 1
            if (n % self._LOG_CLEAN_INTERVAL) == 0:
                self.clean_log()

    @staticmethod
    def grab_id(pkt):
        ret = []
        for field, ftype in rtl_433._ID_FIELDS:
            v = pkt.pop(field, None)
            try:
                v = ftype(v)
            except (ValueError, TypeError):
                pass  # Stick to existing type
            ret.append(v)
        return tuple(ret)

    def metrics(self):
        ret = collections.defaultdict(list)
        now = time.time()
        seen = {}  # value should be most recent timestamp this id has sent data for.
        for ts, id, var, val in reversed(self.log):
            if now - ts > self._MAX_AGE_SECS:
                break
            if now - ts > self._BACKLOG_SECS and ts < seen.get(id, 0):
                continue
            if id not in seen:
                seen[id] = ts

            id_s = ",".join(("%s=\"%s\"" % (f[0], v)) for f, v in zip(self._ID_FIELDS, id) if v is not None)
            if self._EXPORT_TIMESTAMPS:
                ret[var].append("%s{%s} %f %d" % (var, id_s, val, ts * 1000))
            elif ts == seen[id]:
                ret[var].append("%s{%s} %f" % (var, id_s, val))

        return ("\n".join("\n".join(lines) for lines in ret.values())) + "\n"

    def clean_log(self):
        if not self.log:
            return
        if self.log[0][0] - self.log[-1][0] > 60:
            # Time has gone the wrong way by a long time. Drop the table.
            self.log = []
            return

        min_ts = time.time() - self._MAX_AGE_SECS
        for i, e in enumerate(self.log):
            if e[0] >= min_ts:
                break

        # I think this should be safe even if we're serving /metrics since
        # I'm replacing not modifying the log.
        self.log = self.log[i:]


def syslog_reader(udp_port, udp_ip="0.0.0.0"):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind((udp_ip, udp_port))
    while True:
        pkt, _ = sock.recvfrom(4096)
        bits = pkt.split(None, 7)
        if len(bits) == 8:
            yield str(bits[7], "utf-8")


if len(sys.argv) == 1:
    r = rtl_433(sys.stdin)
else:
    r = rtl_433(syslog_reader(int(sys.argv[1])))


class MetricsHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        # boilerplate-- :<
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write(r.metrics().encode("utf-8"))

class MetricsServer(http.server.HTTPServer):
    def handle_error(self, req, addr):
        super().handle_error(req, addr)
        os._exit(6)

https = MetricsServer(("0.0.0.0", _SRV_PORT), MetricsHandler)
httpd = threading.Thread(name="httpd", target=https.serve_forever, daemon=True)
httpd.start()


r.loop()
