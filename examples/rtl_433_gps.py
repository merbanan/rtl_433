#!/usr/bin/env python

"""Read events from rtl_433 and gpsd and print out."""

# Needs gpsd (and the Python support from gpsd)
# Start gpsd and rtl_433 (rtl_433 -F syslog::1433), then this script

from __future__ import print_function

import socket
import time
import json
import gps
import threading

# rtl_433 syslog address
UDP_IP = "127.0.0.1"
UDP_PORT = 1433


class GpsPoller(threading.Thread):
    def __init__(self):
        threading.Thread.__init__(self)
        self.gps = gps.gps(mode=gps.WATCH_ENABLE)
        self.running = True

    def run(self):
        while self.running:
            self.gps.next()

    @property
    def utc(self):
        return self.gps.utc

    @property
    def fix(self):
        return self.gps.fix

    @property
    def satellites(self):
        return self.gps.satellites


def parse_syslog(line):
    """Try to extract the payload from a syslog line."""
    line = line.decode("ascii")  # also UTF-8 if BOM
    if line.startswith("<"):
        # fields should be "<PRI>VER", timestamp, hostname, command, pid, mid, sdata, payload
        fields = line.split(None, 7)
        line = fields[-1]
    return line


def prife(label, data, key):
    """Print if exists."""
    if key in data:
        print(label, data[key])



def report_event(data, gpsp):
    """Print out an rtl_433 event with gps data."""

    # don't process if it isn't sensor data
    if "model" not in data:
        return

    # don't process if it isn't TPMS data
    if "type" not in data:
        return
    if data["type"] != "TPMS":
        return

    # now = int(time.time())
    print("----------------------------------------")
    print("Model          ", data["model"])
    prife("ID             ", data, "id")
    prife("Status         ", data, "status")
    prife("State          ", data, "state")
    prife("Flags          ", data, "flags")
    prife("Code           ", data, "code")
    prife("Pressure (kPa) ", data, "pressure_kPa")
    prife("Pressure (PSI) ", data, "pressure_PSI")
    prife("Temperature (C)", data, "temperature_C")
    prife("Temperature (F)", data, "temperature_F")
    print()
    print("latitude       ", gpsp.fix.latitude)
    print("longitude      ", gpsp.fix.longitude)
    print("time utc       ", gpsp.utc, " + ", gpsp.fix.time)
    print("altitude (m)   ", gpsp.fix.altitude)
    print("eps            ", gpsp.fix.eps)
    print("epx            ", gpsp.fix.epx)
    print("epv            ", gpsp.fix.epv)
    print("ept            ", gpsp.fix.ept)
    print("speed (m/s)    ", gpsp.fix.speed)
    print("climb          ", gpsp.fix.climb)
    print("track          ", gpsp.fix.track)
    print("mode           ", gpsp.fix.mode)
    # print("sats           ", gpsp.satellites)


if __name__ == '__main__':
    gpsp = GpsPoller()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    # allow multiple sockets to use the same PORT number
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    sock.bind((UDP_IP, UDP_PORT))

    try:
        gpsp.start()

        while True:
            line, addr = sock.recvfrom(1024)
            try:
                line = parse_syslog(line)
                data = json.loads(line)
                report_event(data, gpsp)

            except KeyError:
                pass

            except ValueError:
                pass

    except (KeyboardInterrupt, SystemExit): #when you press ctrl+c
        print("\nAborted. Exiting...")
        sock.close()
        gpsp.running = False
        gpsp.join() # wait for the thread to finish

    print("Done.\n")
