#!/usr/bin/env python

"""RRDtool monitoring relay for rtl_433."""

# Start rtl_433 (rtl_433 -C si -F syslog:127.0.0.1:1433), then this script

from __future__ import print_function
from __future__ import with_statement

import sys
import socket
import time
import json
import rrdtool

# Option: PEP 3143 - Standard daemon process library
# (pip install python-daemon)
try:
    import daemon
except ImportError:
    daemon = None

UDP_IP = "127.0.0.1"
UDP_PORT = 1433
RRD_PATH = "" # e.g. "/var/lib/rtl_433/rrd/"
GRAPH_PATH = ""  # e.g. "/var/www/rrd/html/"
GRAPH_INTERVAL = 30 * 60  # in seconds, i.e. 30 minutes


def create_rrd(rrdfile):
    # print("Creating", rrdfile)
    return rrdtool.create(rrdfile,
                          "--step", "1800", "--start", '0',
                          "DS:temperature:GAUGE:2000:U:U",
                          "DS:humidity:GAUGE:2000:U:U",
                          "RRA:AVERAGE:0.5:1:600",
                          "RRA:AVERAGE:0.5:6:700",
                          "RRA:AVERAGE:0.5:24:775",
                          "RRA:AVERAGE:0.5:288:797",
                          "RRA:MAX:0.5:1:600",
                          "RRA:MAX:0.5:6:700",
                          "RRA:MAX:0.5:24:775",
                          "RRA:MAX:0.5:444:797")


def update_rrd(rrdfile, temperature, humidity):
    # print("Updating", rrdfile, temperature, humidity)
    return rrdtool.update(rrdfile, "N:%s:%s" % (temperature, humidity))


def graph_rrd(rrdfile, label, path=""):
    for sched in ['daily' , 'weekly', 'monthly', 'hourly']:
        period = sched[0] # 'w', 'd', 'm', 'h'
        # print("Graphing", sched, label, rrdfile)
        ret = rrdtool.graph("%smetrics-%s.%s.png" % (path, sched, label),
                            "--start", "-1%s" % (period),
                            "--title", label,
                            "--vertical-label=C",
                            "--right-axis-label=%",
                            '--watermark=rtl_433',
                            "-w 800", "-h 200",
                            "DEF:t=%s:temperature:AVERAGE" % (rrdfile),
                            "DEF:h=%s:humidity:AVERAGE" % (rrdfile),
                            "LINE1:t#00FF00:temperature\r",
                            "LINE2:h#0000FF:humidity\r",
                            "GPRINT:t:AVERAGE:Temp avg %6.1lf C",
                            "GPRINT:t:MAX:Temp max %6.1lf C\r",
                            "GPRINT:h:AVERAGE:Hum avg %6.0lf %%",
                            "GPRINT:h:MAX:Hum max %6.0lf %%\r")
    return ret


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


def rtl_433_probe(sock):
    next_graph = {}

    while True:
        line, _addr = sock.recvfrom(1024)

        try:
            line = parse_syslog(line)
            data = json.loads(line)
            now = int(time.time())

            label = sanitize(data["model"])
            if "channel" in data:
                label += ".CH" + str(data["channel"])
            elif "id" in data:
                label += ".ID" + str(data["id"])
            rrd_file = RRD_PATH + label + ".rrd"

            if "type" in data and data["type"] == "TPMS":
                continue

            if "temperature_C" not in data:
                continue
            temperature = data["temperature_C"]

            humidity = "U"
            if "humidity" in data:
                humidity = data["humidity"]

            try:
                rrdtool.info(rrd_file)
            except rrdtool.OperationalError:
                create_rrd(rrd_file)

            update_rrd(rrd_file, temperature, humidity)

            if label not in next_graph or next_graph[label] < now:
                graph_rrd(rrd_file, label, GRAPH_PATH)
                next_graph[label] = now + GRAPH_INTERVAL

        except KeyError:
            pass

        except ValueError:
            pass


def run(run_as_daemon=False):
    # check optional python imports
    if run_as_daemon and not daemon:
        print("Error: pip install python-daemon")
        return

    # setup input and output
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    # allow multiple sockets to use the same PORT number
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    sock.bind((UDP_IP, UDP_PORT))

    # run, as daemon if requested
    if run_as_daemon:
        with daemon.DaemonContext(files_preserve=[sock]):
            rtl_433_probe(sock)
    else:
        rtl_433_probe(sock)


if __name__ == "__main__":
    # simple argument parsing
    run_as_daemon = len(sys.argv) > 1 and sys.argv[1] == "-d"
    try:
        run(run_as_daemon)
    except KeyboardInterrupt:
        pass
