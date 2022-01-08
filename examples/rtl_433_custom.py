#!/usr/bin/env python

"""Custom data handling example for rtl_433."""

# Start rtl_433 (rtl_433 -F syslog::1433), then this script

from __future__ import print_function

import socket
import json

# You can run rtl_433 and this script on different machines,
# start rtl_433 with `-F syslog:YOURTARGETIP:1433`, and change
# to `UDP_IP = "0.0.0.0"` (listen to the whole network) below.
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
    """Listen to all messages in a loop forever."""
    # Open a UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Bind the UDP socket to a listening address
    sock.bind((UDP_IP, UDP_PORT))

    # Loop forever
    while True:
        # Receive a message
        line, addr = sock.recvfrom(1024)

        try:
            # Parse the message format
            line = parse_syslog(line)
            # Decode the message as JSON
            data = json.loads(line)

            #
            # Change for your custom handling below, this is a simple example
            #
            label = data["model"]
            if "channel" in data:
                label += ".CH" + str(data["channel"])
            elif "id" in data:
                label += ".ID" + str(data["id"])

            # E.g. match `model` and `id` to a descriptive name.
            if data["model"] == "LaCrosse-TX" and data["id"] == 123:
                label = "Living Room"

            if "battery_ok" in data:
                if data["battery_ok"] == 0:
                    print(label + ' Battery empty!')

            if "temperature_C" in data:
                print(label + ' Temperature ', data["temperature_C"])

            if "humidity" in data:
                print(label + ' Humidity ', data["humidity"])

            # Ignore unknown message data and continue
        except KeyError:
            pass

        except ValueError:
            pass


if __name__ == "__main__":
    rtl_433_listen()
