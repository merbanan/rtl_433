#!/usr/bin/env python3

"""MQTT monitoring relay for rtl_433 communication."""

# This program listens on a UDP socket for syslog messages with a json
# payload, and publishes the data via MQTT.  The broker connection is
# kept open (and automatically reconnects on failure).  Each device
# is mapped to its own topic,

# Dependencies:
#   Paho-MQTT; see https://pypi.python.org/pypi/paho-mqtt

#   Optionally: PEP 3143 - Standard daemon process library
#      (on 2.7,  pip install python-daemon)

# To enable daemon support, uncomment the following line and adjust
# run().  Note that print() is still used.
# import daemon

from __future__ import print_function
from __future__ import with_statement

import json
import logging
import socket
import time

import paho.mqtt.client as mqtt


# The config class represents a config object.  The constructor takes
# an optional pathname, and will switch on the suffix (.yaml for now)
# and read a dictionary.
class rtlconfig(object):

    # Initialize with default values.
    c = {
        # Log level info (False) or debug (True)
        'DEBUG': False,

        # Address to listen on for syslog/json messages from rtl_433
        'UDP_IP': "127.0.0.1",
        'UDP_PORT': 1433,
        
        # MQTT broker address and credentials
        'MQTT_HOST': "127.0.0.1",
        'MQTT_PORT': 1883,
        'MQTT_USERNAME': None,
        'MQTT_PASSWORD': None,
        'MQTT_TLS': False,

        # MQTT content
        'MQTT_PREFIX': "sensor/rtl_433",
        'MQTT_DEDUP': True,
        'MQTT_INDIVIDUAL_TOPICS': True,
        'MQTT_JSON_TOPIC': True,
    }
    
    def __init__(self, f=None):
        fdict = None

        # Try to read a dictionary from f.
        if f:
            try:
                # Assume yaml. \todo Check and support other formats
                import yaml
                with open(f) as fh:
                    fdict = yaml.safe_load(fh)
            except:
                print('Did not read {f} (no yaml, not found, bad?).'.format(f=f))
            
        # Merge fdict into configdict.
        if fdict:
            for (k, v) in fdict.items():
                self.c[k] = v

    # Support c['name'] references.
    def __getitem__(self, k):
        return self.c[k]

class dedup(object):
    """ A dedup class object supports deduping a stream of reports by
        answering if a report is interesting relative to the history.  While
        more complicated deduping is allowed by the interface, for now it is
        very simple, keeping track of only the previous interesting object.
        For now, we more or less require that all reports have the same keys. """

    # \todo Consider a cache with several entries.

    def __init__(self):
        # Make this long enough to skip repeats, but allow messages
        # every 10s to come through.
        self.duration = 5
        # Exclude reception metadata (time and RF).
        self.boring_keys = ('time', 'freq', 'freq1', 'freq2', 'rssi', 'snr', 'noise', 'raw_msg')
        # Initialize storage for what was last sent.
        (self.last_report, self.last_now) = (None, None)
    
    def send_store(self, report, n):
        """ Record report, n as the last report declared interesting, and
            return True (to denote interesting). """
        (self.last_report, self.last_now) = (report, n)
        return True

    def equiv(self, j1, j2):
        """ Return True if j1 and j2 are the same, except for boring_keys. """
        for (k, v) in j1.items():
            # If in boring, we don't care.
            if k not in self.boring_keys:
                # If in j1 and not j2, they are different.
                if k not in j2:
                    logging.debug("equiv: %s in j1 and not j2" % (k))
                    return False
                if j1[k] != j2[k]:
                    logging.debug("equiv: %s differs j1=%s and j2=%s" % (k, j1[k], j2[k]))
                    return False
        # If the lengths are different, they must be different.
        if len(j1) != len(j2): 
            logging.debug("equiv: len(j1) %d != len(j2) %d" % (len(j1), len(j2)))
            return False

        # If we get here, then the lengths are the same, and all
        # non-boring keys in j1 exist in j2, and have the same value.
        # It could be that j2 is missing a boring key and also has a
        # new non-boring key, but boring keys in particular should not
        # be variable.
        return True

    # report is a python dictionary
    def is_interesting(self, report):
        """ If report is intersting, return True and update records of the
            most recent interesting report.  Otherwise return False. """
        n = time.time()

        # If previous interesting is missing or empty, accept this one.
        if self.last_report is None or self.last_now is None:
            logging.debug("interesting: no previous")
            return self.send_store(report, n)

        # If previous one was too long ago, accept this one.
        if n - self.last_now > self.duration:
            logging.debug("interesting: time")
            return self.send_store(report, n)

        if not self.equiv(self.last_report, report):
            logging.debug("interesting: different")
            return self.send_store(report, n)

        return False

# Create a config object, defaults modified by the config file if present.
c = rtlconfig("rtl_433_mqtt_relay.yaml")

# Create a dedup object for later use, even if it's configured off.
d = dedup()

def mqtt_connect(client, userdata, flags, rc):
    """Handle MQTT connection callback."""
    logging.info("MQTT connected: " + mqtt.connack_string(rc))


def mqtt_disconnect(client, userdata, rc):
    """Handle MQTT disconnection callback."""
    logging.info("MQTT disconnected: " + mqtt.connack_string(rc))


# Create listener for incoming json string packets.
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.bind((c['UDP_IP'], c['UDP_PORT']))


# Map characters that will cause problems or be confusing in mqtt
# topics.
def sanitize(text):
    """Sanitize a name for Graphite/MQTT use."""
    return (text
            .replace(" ", "_")
            .replace("/", "_")
            .replace(".", "_")
            .replace("&", ""))


def publish_sensor_to_mqtt(mqttc, data, line):
    """Publish rtl_433 sensor data to MQTT."""

    if c['MQTT_DEDUP']:
        # If this data is not novel relative to recent data, just skip it.
        # Otherwise, send it via MQTT.
        if not d.is_interesting(data):
            logging.debug("  not interesting")
            return
        logging.debug(  "INTERESTING")

    # Construct a topic from the information that identifies which
    # device this frame is from.
    # NB: id is only used if channel is not present.
    path = c['MQTT_PREFIX']
    if "model" in data:
        path += "/" + sanitize(data["model"])
    if "channel" in data:
        path += "/" + str(data["channel"])
    if "id" in data:
        path += "/" + str(data["id"])

    if c['MQTT_INDIVIDUAL_TOPICS']:
        # Publish some specific items on subtopics.
        if "battery_ok" in data:
            mqttc.publish(path + "/battery", data["battery_ok"])

        if "humidity" in data:
            mqttc.publish(path + "/humidity", data["humidity"])

        if "temperature_C" in data:
            mqttc.publish(path + "/temperature", data["temperature_C"])

        if "depth_cm" in data:
            mqttc.publish(path + "/depth", data["depth_cm"])

    if c['MQTT_JSON_TOPIC']:
        # Publish the entire json string on the main topic.
        mqttc.publish(path, line)

def parse_syslog(line):
    """Try to extract the payload from a syslog line."""
    line = line.decode("ascii")  # also UTF-8 if BOM
    if line.startswith("<"):
        # Fields should be "<PRI>VER", timestamp, hostname, command, pid, mid, sdata, payload.
        # The payload might have spaces, so force split to stop after the sixth space.
        fields = line.split(None, 7)
        line = fields[-1]
    else:
        # Hope that the line was just json without the syslog header.
        pass
    return line


def rtl_433_probe():
    """Run a rtl_433 UDP listener."""

    ## Connect to MQTT
    if hasattr(mqtt, 'CallbackAPIVersion'):  # paho >= 2.0.0
        mqttc = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1)
    else:
        mqttc = mqtt.Client()
    mqttc.on_connect = mqtt_connect
    mqttc.on_disconnect = mqtt_disconnect
    if c['MQTT_USERNAME'] != None:
        mqttc.username_pw_set(c['MQTT_USERNAME'], password=c['MQTT_PASSWORD'])
    if c['MQTT_TLS']:
        mqttc.tls_set()
    mqttc.connect_async(c['MQTT_HOST'], c['MQTT_PORT'], 60)
    mqttc.loop_start()

    ## Receive UDP datagrams, extract json, and publish.
    while True:
        line, addr = sock.recvfrom(1024)
        try:
            line = parse_syslog(line)
            data = json.loads(line)
            logging.debug("received %s" % line)
            publish_sensor_to_mqtt(mqttc, data, line)

        except ValueError:
            pass


def run():
    """Run main or daemon."""
    # with daemon.DaemonContext(files_preserve=[sock]):
    #  detach_process=True
    #  uid
    #  gid
    #  working_directory

    # Set up logging at INFO, and change to DEBUG if config asks for that.
    logging.basicConfig(format='[%(asctime)s] %(levelname)s:%(name)s:%(message)s',datefmt='%Y-%m-%dT%H:%M:%S%z')
    logging.getLogger().setLevel(logging.INFO)
    if c['DEBUG']:
        logging.getLogger().setLevel(logging.DEBUG)
        logging.debug("DEBUG LOGGING ENABLED")

    rtl_433_probe()

if __name__ == "__main__":
    run()
