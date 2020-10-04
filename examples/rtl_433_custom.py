#!/usr/bin/env python

"""Custom data handling example for rtl_433."""

# Start rtl_433 (rtl_433 -F syslog::1433), then this script

from __future__ import print_function

import socket
import json

UDP_IP = "127.0.0.1"
UDP_PORT = 1433


def parse_syslog(line):
    """Try to extract the payload from a syslog line."""
    line = line.decode("ascii")  # also UTF-8 if BOM
    if line.startswith("<"):
        # fields should be "<PRI>VER", timestamp, hostname, command, pid, mid, sdata, payload
        fields = line.split(None, 7)
        line = fields[-1]
    return line


def rtl_433_listen():
    """Try to extract the payload from a syslog line."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))

    while True:
        line, addr = sock.recvfrom(1024)

        try:
            line = parse_syslog(line)
            data = json.loads(line)

            # change for your custom handling below, this is a simple example
            label = data["model"]
            if "channel" in data:
                label += ".CH" + str(data["channel"])
            elif "id" in data:
                label += ".ID" + str(data["id"])

            if "battery_ok" in data:
                if data["battery_ok"] == 0:
                    print(label + ' Battery empty!')

            if "temperature_C" in data:
                print(label + ' Temperature ', data["temperature_C"])

            if "humidity" in data:
                print(label + ' Humidity ', data["humidity"])

        except KeyError:
            pass

        except ValueError:
            pass


if __name__ == "__main__":
    rtl_433_listen()
