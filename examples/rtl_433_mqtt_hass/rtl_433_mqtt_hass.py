#!/usr/bin/env python
# coding=utf-8

"""MQTT Home Assistant auto discovery for rtl_433 events."""

# It is strongly recommended to run rtl_433 with "-C si" and "-M newmodel".

# Needs Paho-MQTT https://pypi.python.org/pypi/paho-mqtt
# Option: PEP 3143 - Standard daemon process library
# (use Python 3.x or pip install python-daemon)
# import daemon

from __future__ import print_function
from __future__ import with_statement

import os
import argparse
import ast
import time
import json
import paho.mqtt.client as mqtt

"""Parse arguments provide sane defaults"""
parser = argparse.ArgumentParser()
parser.add_argument("--host", help="MQTT host", default="127.0.0.1", required=False)
parser.add_argument("--port", help="MQTT port", default=1883, type=int, required=False)
parser.add_argument("--topic", help="MQTT topic", default="rtl_433/+/events", required=False)
parser.add_argument("--username", help="MQTT username", default=None, required=False)
parser.add_argument("--password", help="MQTT password", default=None, required=False)
parser.add_argument("--prefix", help="Discovery prefix", default="homeassistant", required=False)
parser.add_argument("--interval", help="Discovery interval", default=600, type=int, required=False)
parser.add_argument("--mapfile", help="Mapping file", default="example_mappings.json", required=False)
parser.add_argument("-e", help="configure from environment variables", action="store_true", required=False)
args = parser.parse_args()

"""Override Arguments with Env vars if required"""
if args.e:
    args.host = os.getenv('HOST', args.host)
    args.port = int(os.getenv('PORT', args.port))
    args.topic = os.getenv('TOPIC', args.topic)
    args.username = os.getenv('USERNAME', args.username)
    args.password = os.getenv('PASSWORD', args.password)
    args.prefix = os.getenv('prefix', args.prefix)
    args.interval = int(os.getenv('INTERVAL', args.interval))
    args.mapfile = os.getenv('MAPFILE', args.mapfile)

discovery_timeouts = {}

"""Read mappings from file"""
try:
  with open(args.mapfile, "r") as mappingfile:
    mappings = ast.literal_eval(mappingfile.read())
  mappingfile.close()
except:
    print("Could not read mappings.")

def mqtt_connect(client, userdata, flags, rc):
    """Callback for MQTT connects."""
    print("MQTT connected: " + mqtt.connack_string(rc))
    if rc != 0:
        print("Could not connect. Error: " + str(rc))
    else:
        client.subscribe(args.topic)


def mqtt_disconnect(client, userdata, rc):
    """Callback for MQTT disconnects."""
    print("MQTT disconnected: " + mqtt.connack_string(rc))


def mqtt_message(client, userdata, msg):
    """Callback for MQTT message PUBLISH."""
    try:
        # Decode JSON payload
        data = json.loads(msg.payload.decode())
        topicprefix = "/".join(msg.topic.split("/", 2)[:2])
        bridge_event_to_hass(client, topicprefix, data)

    except json.decoder.JSONDecodeError:
        print("JSON decode error: " + msg.payload.decode())
        return


def sanitize(text):
    """Sanitize a name for Graphite/MQTT use."""
    return (text
            .replace(" ", "_")
            .replace("/", "_")
            .replace(".", "_")
            .replace("&", ""))


def publish_config(mqttc, topic, model, instance, mapping):
    """Publish Home Assistant auto discovery data."""
    global discovery_timeouts

    instance_no_slash = instance.replace("/", "-")
    device_type = mapping["device_type"]
    object_suffix = mapping["object_suffix"]
    object_id = "-".join([model, instance_no_slash])
    object_name = "-".join([object_id,object_suffix])

    path = "/".join([args.prefix, device_type, object_id, object_name, "config"])

    # check timeout
    now = time.time()
    if path in discovery_timeouts:
        if discovery_timeouts[path] > now:
            return

    discovery_timeouts[path] = now + args.interval

    config = mapping["config"].copy()
    config["name"] = object_name
    config["state_topic"] = topic
    config["unique_id"] = object_name
    config["device"] = { "identifiers": object_id, "name": object_id, "manufacturer": "rtl_433" }

    mqttc.publish(path, json.dumps(config))
    print(path, " : ", json.dumps(config))


def bridge_event_to_hass(mqttc, topicprefix, data):
    """Translate some rtl_433 sensor data to Home Assistant auto discovery."""

    if "model" not in data:
        # not a device event
        return
    model = sanitize(data["model"])
    instance = None

    if "channel" in data:
        channel = str(data["channel"])
        instance = channel
    if "id" in data:
        device_id = str(data["id"])
        if not instance:
            instance = device_id
        else:
            instance = channel + "/" + device_id
    if not instance:
        # no unique device identifier
        return

    # detect known attributes
    for key in data.keys():
        if key in mappings:
            topic = "/".join([topicprefix,"devices",model,instance,key])
            publish_config(mqttc, topic, model, instance, mappings[key])


def rtl_433_bridge():
    """Run a MQTT Home Assistant auto discovery bridge for rtl_433."""
    mqttc = mqtt.Client()
    if args.username is not None:
        mqttc.username_pw_set(args.username, args.password)
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
    run()
