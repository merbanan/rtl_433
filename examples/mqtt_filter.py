#!/usr/bin/env python3
""" MQTT republishing filter to rename and deduplicate rtl_433 data

Example program for receiving, filtering and republishing sensor data
from rtl_433 using MQTT network messages.

An MQTT broker e.g. 'mosquitto' must be running and rtl_433 must publish
to that broker, e.g. using -F mqtt.
"""

import json
import logging
import sys
import time
import socket
import re

import paho.mqtt.client as mqtt
from paho.mqtt.subscribeoptions import SubscribeOptions

HOSTNAME = socket.getfqdn()

MQTT_SERVER = "127.0.0.1"  # set your MQTT broker address here

MQTT_TOPIC_PREFIX = "rtl_433/+"  # default to use all rtl_433 senders
# MQTT_TOPIC_PREFIX = "rtl_433/" + HOSTNAME  # alternative: use just the local host topics
# MQTT_TOPIC_PREFIX = "rtl_433/MYSERVER"  # alternativ: use a named rtl_433 sender

MQTT_TOPIC_DEVICES = MQTT_TOPIC_PREFIX + "/devices"  # default "devices" topic tree base
MQTT_TOPIC_EVENTS = MQTT_TOPIC_PREFIX + "/events"  # default "events" topic

# set source and target topics as well as deduplication in seconds, 0 disables deduplication
DEVICE_MAPPINGS = [
    { "source": "Bresser-3CH/1/130", "target": "Livingroom", "dedup": 1.5},
    { "source": "Bresser-3CH/2/175", "target": "Bedroom", "dedup": 1.5},
    { "source": "LaCrosse-TX141THBv2/2/122", "target": "Garage", "dedup": 0},
]

EVENT_MAPPINGS = [
    { "source": { "model": "Bresser-3CH", "id": 130 }, "target": { "nickname": "Livingroom" }, "dedup": 1.5},
    { "source": { "model": "Bresser-3CH", "id": 175 }, "target": { "nickname": "Bedroom" }, "dedup": 1.5},
    { "source": { "model": "LaCrosse-TX141THBv2", "id": 122 }, "target": { "nickname": "Garage" }, "dedup": 0},
]

EVENT_REPUBLISHED_KEY = "republished"  # a JSON key only present in republished messages
# EVENT_REPUBLISHED_KEY = "nickname"  # can also be key added with "target"

if hasattr(mqtt, 'CallbackAPIVersion'):  # paho >= 2.0.0
    mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1, client_id="RTL_433_Filter")
else:
    mqtt_client = mqtt.Client(client_id="RTL_433_Filter")

DEVICES_RE = MQTT_TOPIC_DEVICES.replace("+", ".+")
EVENTS_RE = MQTT_TOPIC_EVENTS.replace("+", ".+")

dedup_cache = {}


def publish_dedup(topic, payload, timeout):
    """ Deduplicate and publish a message. """
    global dedup_cache
    global mqtt_client

    if timeout <= 0:
        logging.debug("republishing " + topic + " : " + str(payload))
        mqtt_client.publish(topic, payload)
        return

    key = topic + str(payload)
    now = time.time()
    if key in dedup_cache and dedup_cache[key] > now:
        logging.info("dedup " + topic + " : " + str(payload))
    else:
        logging.debug("republishing " + topic + " : " + str(payload))
        mqtt_client.publish(topic, payload)

    dedup_cache[key] = now + timeout
    for k in list(dedup_cache.keys()):
        if dedup_cache[k] < now:
            del dedup_cache[k]


def filter_devices(topic, payload):
    """ Deduplicate and republish device messages. """
    global mqtt_client

    if not re.match(DEVICES_RE, topic):
        return

    # Loop through all device mappings
    for map in DEVICE_MAPPINGS:
        path = "(" + DEVICES_RE + "/)" + map["source"] + "(.+)"
        m = re.match(path, topic)
        # On match republish same payload to different topic
        if m:
            target = m.group(1) + map["target"] + m.group(2)
            logging.debug("republishing " + map["source"] + " : " + target)
            publish_dedup(target, payload, map["dedup"])


def is_sub_dict(needle, haystack):
    """ Test if all key-value pairs of needle match haystack. """
    for k, v in needle.items():
        if k not in haystack or haystack[k] != v:
            return False
    return True


def filter_events(topic, payload):
    """ Deduplicate and republish event messages. """
    global mqtt_client

    if not re.match(EVENTS_RE, topic):
        return

    try:
        data = json.loads(payload.decode())

        # MQTT v5 noLocal might not work
        # Ignore republished events, there should be a better way?
        if EVENT_REPUBLISHED_KEY in data:
            return

        # Loop through all event mappings
        for map in EVENT_MAPPINGS:
            # Ensure all JSON data keys match
            if not is_sub_dict(map["source"], data):
                continue
            # On match republish modified payload to same topic

            # Tag as repbulished event
            data[EVENT_REPUBLISHED_KEY] = 1
            # Add all "target" keys, e.g. a nickname
            for k, v in map["target"].items():
                data[k] = v
            logging.debug("republishing to " + topic)
            publish_dedup(topic, json.dumps(data), map["dedup"])

    except json.decoder.JSONDecodeError:
        logging.warning("JSON decode error: " + payload.decode())


def on_connect(client, userdata, flags, rc):
    """ Callback for when the client receives a CONNACK response from the server. """
    logging.info("MQTT Connection: " + mqtt.connack_string(rc))
    if rc != 0:
        logging.error("Could not connect. RC: " + str(rc))
        exit()
    # Subscribing in on_connect() means that if we lose the connection and reconnect then subscriptions will be renewed.
    options = SubscribeOptions(qos=1, noLocal=True)
    logging.info("Subscribing to " + MQTT_TOPIC_EVENTS)
    client.subscribe(MQTT_TOPIC_EVENTS, options=options)
    logging.info("Subscribing to " + MQTT_TOPIC_DEVICES + "/#")
    client.subscribe(MQTT_TOPIC_DEVICES + "/#", options=options)


def on_disconnect(client, userdata, rc):
    if rc != 0:
        logging.error("Unexpected disconnection. RC: " + str(rc))


def on_message(client, userdata, msg):
    """ Callback for when a PUBLISH message is received from the server. """
    logging.debug("Received: " + msg.topic + "\t" + msg.payload.decode())
    filter_devices(msg.topic, msg.payload)
    filter_events(msg.topic, msg.payload)


# Setup MQTT client
mqtt_client.on_connect = on_connect
mqtt_client.on_disconnect = on_disconnect
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_SERVER)
mqtt_client.loop_start()


def main():
    """MQTT republishing filter"""
    logging.basicConfig(format='[%(asctime)s] %(levelname)s:%(name)s:%(message)s',datefmt='%Y-%m-%dT%H:%M:%S%z')
    logging.getLogger().setLevel(logging.INFO)

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
