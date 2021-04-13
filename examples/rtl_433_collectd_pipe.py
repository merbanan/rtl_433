#!/usr/bin/env python

"""Collectd monitoring probe (i.e. no plugin) for rtl_433."""

# Needs Python collectd Network plugin, s.a. https://github.com/appliedsec/collectd
#   pip install collectd
# -or-
#   curl -O https://github.com/appliedsec/collectd/raw/master/collectd.py

import time
import socket
import fileinput
import json
import collectd


def send_stats(when, stats, sender, to):
    for (plugin_type, plugin_inst), values in stats.items():
        if not values:
            continue
        collectd.PLUGIN_TYPE = plugin_type
        for message in collectd.messages(values, when, sender, plugin_inst):
            collectd.sock.sendto(message, to)


def sanitize(text):
    return text.replace(" ", "_")


def rtl_433_probe():
    hostname = socket.getfqdn()
    interval = 60.0  # seconds

    collectd.SEND_INTERVAL = interval
    collectd.PLUGIN_NAME = 'rtlsdr'

    collectd_host = "localhost"
    collectd_port = 25826

    for line in fileinput.input():
        try:
            data = json.loads(line)

            when = int(time.time())
            label = sanitize(data["model"])
            if "channel" in data:
                label += ".CH" + str(data["channel"])
            attributes = {}
            temperatures = {}

            if data["battery"] == "OK":
                attributes["battery"] = 1.0
            else:
                attributes["battery"] = 0.0

            attributes["humidity"] = data["humidity"]

            temperatures["sensor"] = data["temperature_C"]

            stats = {('gauge', label): attributes,
                     ('temperature', label): temperatures}

            send_stats(when, stats, hostname, (collectd_host, collectd_port))

        except ValueError:
            pass


if __name__ == "__main__":
    rtl_433_probe()
