#!/usr/bin/env python

"""Statsd monitoring for rtl_433 using pipes."""

# Needs Python statsd Network plugin, s.a. https://github.com/jsocol/pystatsd
#   pip install pystatsd
# -or-
#   curl -o statsd.py https://github.com/jsocol/pystatsd/raw/v3.2/statsd/client.py

import sys
import json
from statsd import StatsClient


def sanitize(text):
    return text.replace(" ", "_")


def rtl_433_probe():
    statsd_host = "localhost"
    statsd_host = "127.0.0.1"
    statsd_port = 8125
    statsd_prefix = 'rtlsdr'

    statsd = StatsClient(host=statsd_host,
                         port=statsd_port,
                         prefix=statsd_prefix)

    while True:
        line = sys.stdin.readline()
        if not line:
            break
        try:
            data = json.loads(line)

            label = sanitize(data["model"])
            if "channel" in data:
                label += ".CH" + str(data["channel"])

            if "battery" in data:
                if data["battery"] == "OK":
                    statsd.gauge(label + '.battery', 1)
                else:
                    statsd.gauge(label + '.battery', 0)

            if "humidity" in data:
                statsd.gauge(label + '.humidity', data["humidity"])

            statsd.gauge(label + '.temperature', data["temperature_C"])

        except KeyError:
            pass

        except ValueError:
            pass


if __name__ == "__main__":
    rtl_433_probe()
