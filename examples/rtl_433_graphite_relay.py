#!/usr/bin/env python

"""Graphite(Carbon) monitoring relay for rtl_433."""

# Start rtl_433 (rtl_433 -F syslog::1433), then this script

# Option: PEP 3143 - Standard daemon process library
# (use Python 3.x or pip install python-daemon)
# import daemon

from __future__ import print_function
from __future__ import with_statement

import socket
import time
import json

UDP_IP = "127.0.0.1"
UDP_PORT = 1433
GRAPHITE_HOST = "127.0.0.1"
GRAPHITE_PORT = 2003
GRAPHITE_PREFIX = "rtlsdr."


class GraphiteUdpClient(object):
    def __init__(self, host='localhost', port=2003, ipv6=False):
        """Create a new client."""
        fam = socket.AF_INET6 if ipv6 else socket.AF_INET
        family, _, _, _, addr = socket.getaddrinfo(
            host, port, fam, socket.SOCK_DGRAM)[0]
        self._addr = addr
        self._sock = socket.socket(family, socket.SOCK_DGRAM)

    def _send(self, message):
        """Send raw data to graphite."""
        try:
            self._sock.sendto(message, self._addr)
        except (socket.error, RuntimeError):
            pass

    def push(self, path, value, timestamp=None):
        """Send a value to graphite."""
        if not timestamp:
            timestamp = int(time.time())

        message = "{0} {1} {2}".format(path, value, timestamp)
        self._send(message)


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
# allow multiple sockets to use the same PORT number
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
sock.bind((UDP_IP, UDP_PORT))


def sanitize(text):
    return text.replace(" ", "_").replace("/", "_").replace(".", "_").replace("&", "")


def parse_syslog(line):
    """Try to extract the payload from a syslog line."""
    line = line.decode("ascii")  # also UTF-8 if BOM
    if line.startswith("<"):
        # fields should be "<PRI>VER", timestamp, hostname, command, pid, mid, sdata, payload
        fields = line.split(None, 7)
        line = fields[-1]
    return line


def rtl_433_probe():
    graphite = GraphiteUdpClient(host=GRAPHITE_HOST,
                                 port=GRAPHITE_PORT)

    while True:
        line, addr = sock.recvfrom(1024)

        try:
            line = parse_syslog(line)
            data = json.loads(line)
            now = int(time.time())

            label = sanitize(data["model"])
            if "channel" in data:
                label += ".CH" + str(data["channel"])
            elif "id" in data:
                label += ".ID" + str(data["id"])
            path = GRAPHITE_PREFIX + label

            if "battery" in data:
                if data["battery"] == "OK":
                    graphite.push(path + '.battery', 1, now)
                else:
                    graphite.push(path + '.battery', 0, now)

            if "humidity" in data:
                graphite.push(path + '.humidity', data["humidity"], now)

            graphite.push(path + '.temperature', data["temperature_C"], now)

            # graphite.commit()  # for Pickle protocol only

        except KeyError:
            pass

        except ValueError:
            pass


def run():
    # with daemon.DaemonContext(files_preserve=[sock]):
    #  detach_process=True
    #  uid
    #  gid
    #  working_directory
    rtl_433_probe()


if __name__ == "__main__":
    run()
