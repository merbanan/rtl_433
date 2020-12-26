#!/usr/bin/env python
# coding=utf-8

from __future__ import print_function
from __future__ import with_statement

AP_DESCRIPTION="""
Publish Home Assistant MQTT auto discovery topics for rtl_433 devices.

rtl_433_mqtt_hass.py connects to MQTT and subscribes to the rtl_433
event stream that is published to MQTT by rtl_433. The script publishes
additional MQTT topics that can be used by Home Assistant to automatically
discover and minimally configure new devices.

The configuration topics published by this script tell Home Assistant
what MQTT topics to subscribe to in order to receive the data published
as device topics by MQTT.
"""

AP_EPILOG="""
It is strongly recommended to run rtl_433 with "-C si" and "-M newmodel".
This script requires rtl_433 to publish both event messages and device
messages.

MQTT Username and Password can be set via the cmdline or passed in the
environment: MQTT_USERNAME and MQTT_PASSWORD.

Prerequisites:

1. rtl_433 running separately publishing events and devices messages to MQTT.

2. Python installation
* Python 3.x preferred.
* Needs Paho-MQTT https://pypi.python.org/pypi/paho-mqtt

  Debian/raspbian:  apt install python3-paho-mqtt
  Or
  pip install paho-mqtt
* Optional for running as a daemon see PEP 3143 - Standard daemon process library
  (use Python 3.x or pip install python-daemon)


Running:

This script can run continually as a daemon, where it will publish
a configuration topic for the device events sent to MQTT by rtl_433
every 10 minutes.

Alternatively if the rtl_433 devices in your environment change infrequently
this script can use the MQTT retain flag to make the configuration topics
persistent. The script will only need to be run when things change or if
the MQTT server loses its retained messages.

Getting rtl_433 devices back after Home Assistant restarts will happen
more quickly if MQTT retain is enabled. Note however that definitions
for any transitient devices/false positives will retained indefinitely.


Suggestions:

Running this script will cause a number of Home Assistant entities (sensors
and binary sensors) to be created. These entities can linger for a while unless
the topic is republished with an empty config string.  To avoid having to
do a lot of clean up When running this initially or debugging, set this
script to publish to a topic other than the one Home Assistant users (homeassistant).

MQTT Explorer (http://http://mqtt-explorer.com/) is a very nice GUI for
working with MQTT. It is free, cross platform, and OSS. The structured
hierarchical view makes it easier to understand what rtl_433 is publishing
and how this script works with Home Assistant.

MQTT Explorer also makes it easy to publish an empty config topic to delete an
entity from Home Assistant.


Known Issues:

Currently there is no white or black lists, so any device that rtl_433 receives
including transients, false positives, will create a bunch of entities in
Home Assistant.

As of 2020-10, Home Assistant MQTT auto discovery doesn't currently support
supplying "friendly name", and "area" key, so some configuration must be
done in Home Assistant.

There is a single global set of field mappings to Home Assistant meta data.

"""



# import daemon


import os
import argparse
import time
import json
import paho.mqtt.client as mqtt


discovery_timeouts = {}

# Fields used for creating topic names
NAMING_KEYS = [ "type", "model", "subtype", "channel", "id" ]

# Fields that get ignored when publishing to Home Assistant
# (reduces noise to help spot missing field mappings)
SKIP_KEYS = NAMING_KEYS + [ "time", "mic", "mod", "freq", "sequence_num",
                            "message_type", "exception", "raw_msg" ]


# Global mapping of rtl_433 field names to Home Assistant metadata.
# @todo - should probably externalize to a config file
# @todo - Model specific definitions might be needed

mappings = {
    "temperature_C": {
        "device_type": "sensor",
        "object_suffix": "T",
        "config": {
            "device_class": "temperature",
            "name": "Temperature",
            "unit_of_measurement": "°C",
            "value_template": "{{ value|float }}"
        }
    },
    "temperature_1_C": {
        "device_type": "sensor",
        "object_suffix": "T1",
        "config": {
            "device_class": "temperature",
            "name": "Temperature 1",
            "unit_of_measurement": "°C",
            "value_template": "{{ value|float }}"
        }
    },
    "temperature_2_C": {
        "device_type": "sensor",
        "object_suffix": "T2",
        "config": {
            "device_class": "temperature",
            "name": "Temperature 2",
            "unit_of_measurement": "°C",
            "value_template": "{{ value|float }}"
        }
    },
    "temperature_F": {
        "device_type": "sensor",
        "object_suffix": "F",
        "config": {
            "device_class": "temperature",
            "name": "Temperature",
            "unit_of_measurement": "°F",
            "value_template": "{{ value|float }}"
        }
    },

    "battery_ok": {
        "device_type": "sensor",
        "object_suffix": "B",
        "config": {
            "device_class": "battery",
            "name": "Battery",
            "unit_of_measurement": "%",
            "value_template": "{{ float(value|int) * 99 + 1 }}"
        }
    },

    "humidity": {
        "device_type": "sensor",
        "object_suffix": "H",
        "config": {
            "device_class": "humidity",
            "name": "Humidity",
            "unit_of_measurement": "%",
            "value_template": "{{ value|float }}"
        }
    },

    "moisture": {
        "device_type": "sensor",
        "object_suffix": "H",
        "config": {
            "device_class": "moisture",
            "name": "Moisture",
            "unit_of_measurement": "%",
            "value_template": "{{ value|float }}"
        }
    },

    "pressure_hPa": {
        "device_type": "sensor",
        "object_suffix": "P",
        "config": {
            "device_class": "pressure",
            "name": "Pressure",
            "unit_of_measurement": "hPa",
            "value_template": "{{ value|float }}"
        }
    },

    "wind_speed_km_h": {
        "device_type": "sensor",
        "object_suffix": "WS",
        "config": {
            "name": "Wind Speed",
            "unit_of_measurement": "km/h",
            "value_template": "{{ value|float }}"
        }
    },

    "wind_avg_km_h": {
        "device_type": "sensor",
        "object_suffix": "WS",
        "config": {
            "device_class": "weather",
            "name": "Wind Speed",
            "unit_of_measurement": "km/h",
            "value_template": "{{ value|float }}"
        }
    },

    "wind_avg_mi_h": {
        "device_type": "sensor",
        "object_suffix": "WS",
        "config": {
            "device_class": "weather",
            "name": "Wind Speed",
            "unit_of_measurement": "mi/h",
            "value_template": "{{ value|float }}"
        }
    },

    "wind_avg_m_s": {
        "device_type": "sensor",
        "object_suffix": "WS",
        "config": {
            "name": "Wind Average",
            "unit_of_measurement": "km/h",
            "value_template": "{{ float(value|float) * 3.6 | round(2) }}"
        }
    },

    "wind_speed_m_s": {
        "device_type": "sensor",
        "object_suffix": "WS",
        "config": {
            "name": "Wind Speed",
            "unit_of_measurement": "km/h",
            "value_template": "{{ float(value|float) * 3.6 }}"
        }
    },

    "gust_speed_km_h": {
        "device_type": "sensor",
        "object_suffix": "GS",
        "config": {
            "name": "Gust Speed",
            "unit_of_measurement": "km/h",
            "value_template": "{{ value|float }}"
        }
    },

    "wind_max_m_s": {
        "device_type": "sensor",
        "object_suffix": "GS",
        "config": {
            "name": "Wind max",
            "unit_of_measurement": "km/h",
            "value_template": "{{ float(value|float) * 3.6 | round(2) }}"
        }
    },

    "gust_speed_m_s": {
        "device_type": "sensor",
        "object_suffix": "GS",
        "config": {
            "name": "Gust Speed",
            "unit_of_measurement": "km/h",
            "value_template": "{{ float(value|float) * 3.6 }}"
        }
    },

    "wind_dir_deg": {
        "device_type": "sensor",
        "object_suffix": "WD",
        "config": {
            "name": "Wind Direction",
            "unit_of_measurement": "°",
            "value_template": "{{ value|float }}"
        }
    },

    "rain_mm": {
        "device_type": "sensor",
        "object_suffix": "RT",
        "config": {
            "name": "Rain Total",
            "unit_of_measurement": "mm",
            "value_template": "{{ value|float }}"
        }
    },

    "rain_mm_h": {
        "device_type": "sensor",
        "object_suffix": "RR",
        "config": {
            "name": "Rain Rate",
            "unit_of_measurement": "mm/h",
            "value_template": "{{ value|float }}"
        }
    },

    "rain_in": {
        "device_type": "sensor",
        "object_suffix": "RT",
        "config": {
            "name": "Rain Total",
            "unit_of_measurement": "mm",
            "value_template": "{{ float(value|float) * 25.4 | round(2) }}"
        }
    },

    "rain_rate_in_h": {
        "device_type": "sensor",
        "object_suffix": "RR",
        "config": {
            "name": "Rain Rate",
            "unit_of_measurement": "mm/h",
            "value_template": "{{ float(value|float) * 25.4 | round(2) }}"
        }
    },

    "tamper": {
        "device_type": "binary_sensor",
        "object_suffix": "tamper",
        "config": {
            "device_class": "safety",
            "force_update": "true",
            "payload_on": "1",
            "payload_off": "0"
        }
    },

    "alarm": {
        "device_type": "binary_sensor",
        "object_suffix": "alarm",
        "config": {
            "device_class": "safety",
            "force_update": "true",
            "payload_on": "1",
            "payload_off": "0"
        }
    },

    "rssi": {
        "device_type": "sensor",
        "object_suffix": "rssi",
        "config": {
            "device_class": "signal_strength",
            "unit_of_measurement": "dB",
            "value_template": "{{ value|float|round(2) }}"
        }
    },

    "snr": {
        "device_type": "sensor",
        "object_suffix": "snr",
        "config": {
            "device_class": "signal_strength",
            "unit_of_measurement": "dB",
            "value_template": "{{ value|float|round(2) }}"
        }
    },

    "noise": {
        "device_type": "sensor",
        "object_suffix": "noise",
        "config": {
            "device_class": "signal_strength",
            "unit_of_measurement": "dB",
            "value_template": "{{ valuei|float|round(2) }}"
        }
    },

    "depth_cm": {
        "device_type": "sensor",
        "object_suffix": "D",
        "config": {
            "device_class": "depth",
            "name": "Depth",
            "unit_of_measurement": "cm",
            "value_template": "{{ value|float }}"
        }
    },

    "power_W": {
        "device_type": "sensor",
        "object_suffix": "watts",
        "config": {
            "device_class": "power",
            "name": "Power",
            "unit_of_measurement": "W",
            "value_template": "{{ value|float }}"
        }
    },

    "lux": {
        "device_type": "sensor",
        "object_suffix": "lux",
        "config": {
            "device_class": "weather",
            "name": "Outside Luminancee",
            "unit_of_measurement": "lux",
            "value_template": "{{ value|int }}"
        }
    },

    "uv": {
        "device_type": "sensor",
        "object_suffix": "uv",
        "config": {
            "device_class": "weather",
            "name": "UV Index",
            "unit_of_measurement": "UV Index",
            "value_template": "{{ value|int }}"
        }
    },

    "storm_dist": {
        "device_type": "sensor",
        "object_suffix": "stdist",
        "config": {
            "name": "Lightning Distance",
            "unit_of_measurement": "mi",
            "value_template": "{{ value|int }}"
        }
    },

    "strike_distance": {
        "device_type": "sensor",
        "object_suffix": "stdist",
        "config": {
            "name": "Lightning Distance",
            "unit_of_measurement": "mi",
            "value_template": "{{ value|int }}"
        }
    },

    "strike_count": {
        "device_type": "sensor",
        "object_suffix": "strcnt",
        "config": {
            "name": "Lightning Strike Count",
            "value_template": "{{ value|int }}"
        }
    },
}


def mqtt_connect(client, userdata, flags, rc):
    """Callback for MQTT connects."""

    print("MQTT connected: " + mqtt.connack_string(rc))
    if rc != 0:
        print("Could not connect. Error: " + str(rc))
    else:
        client.subscribe(args.rtl_topic)


def mqtt_disconnect(client, userdata, rc):
    """Callback for MQTT disconnects."""
    print("MQTT disconnected: " + mqtt.connack_string(rc))


def mqtt_message(client, userdata, msg):
    """Callback for MQTT message PUBLISH."""
    try:
        # Decode JSON payload
        data = json.loads(msg.payload.decode())

    except json.decoder.JSONDecodeError:
        print("JSON decode error: " + msg.payload.decode())
        return

    topicprefix = "/".join(msg.topic.split("/", 2)[:2])
    bridge_event_to_hass(client, topicprefix, data)


def sanitize(text):
    """Sanitize a name for Graphite/MQTT use."""
    return (text
            .replace(" ", "_")
            .replace("/", "_")
            .replace(".", "_")
            .replace("&", ""))

def rtl_433_device_topic(data):
    """Return rtl_433 device topic to subscribe to for a data element"""

    path_elements = []

    for key in NAMING_KEYS:
        if key in data:
            element = sanitize(str(data[key]))
            path_elements.append(element)

    return '/'.join(path_elements)


def publish_config(mqttc, topic, model, instance, mapping):
    """Publish Home Assistant auto discovery data."""
    global discovery_timeouts

    instance_no_slash = instance.replace("/", "-")
    device_type = mapping["device_type"]
    object_suffix = mapping["object_suffix"]
    object_id = instance_no_slash
    object_name = "-".join([object_id,object_suffix])

    path = "/".join([args.discovery_prefix, device_type, object_id, object_name, "config"])

    # check timeout
    now = time.time()
    if path in discovery_timeouts:
        if discovery_timeouts[path] > now:
            return False

    discovery_timeouts[path] = now + args.discovery_interval

    config = mapping["config"].copy()
    config["name"] = object_name
    config["state_topic"] = topic
    config["unique_id"] = object_name
    config["device"] = { "identifiers": object_id, "name": object_id, "model": model, "manufacturer": "rtl_433" }

    if args.debug:
        print(path,":",json.dumps(config))

    mqttc.publish(path, json.dumps(config), args.retain)

    return True

def bridge_event_to_hass(mqttc, topicprefix, data):
    """Translate some rtl_433 sensor data to Home Assistant auto discovery."""

    if "model" not in data:
        # not a device event
        return

    model = sanitize(data["model"])

    skipped_keys = []
    published_keys = []

    instance = rtl_433_device_topic(data)
    if not instance:
        # no unique device identifier
        if not args.quiet:
            print("No suitable identifier found for model: ", model)
        return

    # detect known attributes
    for key in data.keys():
        if key in mappings:
            # topic = "/".join([topicprefix,"devices",model,instance,key])
            topic = "/".join([topicprefix,"devices",instance,key])
            if publish_config(mqttc, topic, model, instance, mappings[key]):
                published_keys.append(key)
        else:
            if key not in SKIP_KEYS:
                skipped_keys.append(key)

    if published_keys and not args.quiet:
        print("Published %s: %s" % (instance, ", ".join(published_keys)))

        if skipped_keys and not args.quiet:
            print("Skipped %s: %s" % (instance, ", ".join(skipped_keys)))


def rtl_433_bridge():
    """Run a MQTT Home Assistant auto discovery bridge for rtl_433."""

    mqttc = mqtt.Client()
    if args.user is not None:
        mqttc.username_pw_set(args.user, args.password)
    mqttc.on_connect = mqtt_connect
    mqttc.on_disconnect = mqtt_disconnect
    mqttc.on_message = mqtt_message
    mqttc.connect_async(args.host, args.port, 60)
    mqttc.loop_start()

    while True:
        time.sleep(1)


def run():
    """Run main or daemon."""
    # with daemon.DaemonContext(files_preserve=[sock]):
    #  detach_process=True
    #  uid
    #  gid
    #  working_directory
    rtl_433_bridge()


if __name__ == "__main__":

    parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                     description=AP_DESCRIPTION,
                                     epilog=AP_EPILOG)

    parser.add_argument("-d", "--debug", action="store_true")
    parser.add_argument("-q", "--quiet", action="store_true")
    parser.add_argument("-u", "--user", type=str, help="MQTT username")
    parser.add_argument("-P", "--password", type=str, help="MQTT password")
    parser.add_argument("-H", "--host", type=str, default="127.0.0.1",
                        help="MQTT hostname to connect to (default: %(default)s)")
    parser.add_argument("-p", "--port", type=int, default=1883,
                        help="MQTT port (default: %(default)s)")
    parser.add_argument("-r", "--retain", action="store_true")
    parser.add_argument("-R", "--rtl-topic", type=str,
                        default="rtl_433/+/events",
                        dest="rtl_topic",
                        help="rtl_433 MQTT event topic to subscribe to (default: %(default)s)")
    parser.add_argument("-D", "--discovery-prefix", type=str,
                        dest="discovery_prefix",
                        default="homeassistant",
                        help="Home Assistant MQTT topic prefix (default: %(default)s)")
    parser.add_argument("-i", "--interval", type=int,
                        dest="discovery_interval",
                        default=600,
                        help="Interval to republish config topics in seconds (default: %(default)d)")
    args = parser.parse_args()

    # allow setting MQTT username and password via environment variables
    if not args.user and 'MQTT_USERNAME' in os.environ:
        args.user = os.environ['MQTT_USERNAME']

    if not args.password and 'MQTT_PASSWORD' in os.environ:
        args.password = os.environ['MQTT_PASSWORD']

    run()
