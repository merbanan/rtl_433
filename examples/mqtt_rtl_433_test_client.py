#!/usr/bin/env python3
""" MQTT test client for receiving rtl_433 JSON data

Example program for receiving and parsing sensor data from rtl_433 sent
as MQTT network messages. Recommended way of sending rtl_433 data on network is:

$ rtl_433 -F json -M utc | mosquitto_pub -t home/rtl_433 -l

An MQTT broker e.g. 'mosquitto' must be running on local computer

Copyright (C) 2017 Tommy Vestermark
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
"""

import datetime
import json
import logging
import multiprocessing as mp
import sys
import time

import paho.mqtt.client as mqtt

MQTT_SERVER = "127.0.0.1"
MQTT_TOPIC_PREFIX = "home/rtl_433"
TIMEOUT_STALE_SENSOR = 600  # Seconds before showing a timeout indicator

# log = logging.getLogger()  # Single process logger
log = mp.log_to_stderr()  # Multiprocessing capable logger
mqtt_client = mqtt.Client("RTL_433_Test")

sensor_state = dict()  # Dictionary containing accumulated sensor state


def print_sensor_state():
    """ Print accumulated sensor state """
    time_now = datetime.datetime.utcnow().replace(microsecond=0)
    print("\nUpdate per {} UTC".format(time_now.isoformat(sep=' ')))
    for model in sensor_state:
        print(model)
        for ID in sensor_state[model]:
            data = sensor_state[model][ID]['data'].copy()
            timestamp = data.pop('time')
            timedelta = (time_now - timestamp).total_seconds()
            indicator = "*" if (timedelta < 2) else "~" if (timedelta > TIMEOUT_STALE_SENSOR) else " "  # Indicator for new and stale data
            print("  ID {:5} {}{} {}".format(ID, timestamp.isoformat(sep=' '), indicator, data))
    sys.stdout.flush()  # Print in real-time


def on_connect(client, userdata, flags, rc):
    """ Callback for when the client receives a CONNACK response from the server. """
    log.info("MQTT Connection: " + mqtt.connack_string(rc))
    if rc != 0:
        log.error("Could not connect. RC: " + str(rc))
        exit()
    # Subscribing in on_connect() means that if we lose the connection and reconnect then subscriptions will be renewed.
    client.subscribe(MQTT_TOPIC_PREFIX)


def on_disconnect(client, userdata, rc):
    if rc != 0:
        log.error("Unexpected disconnection. RC: " + str(rc))


def on_message(client, userdata, msg):
    """ Callback for when a PUBLISH message is received from the server. """
    if msg.topic.startswith(MQTT_TOPIC_PREFIX):
        try:
            # Decode JSON payload
            d = json.loads(msg.payload.decode())
        except json.decoder.JSONDecodeError:
            log.warning("JSON decode error: " + msg.payload.decode())
            return

        # Convert time string to datetime object
        time_str = d.get('time', "0000-00-00 00:00:00")
        time_utc = datetime.datetime.strptime(time_str, "%Y-%m-%d %H:%M:%S")
        d['time'] = time_utc
        # Update sensor_state
        sensor_model = d.pop('model', 'unknown')
        sensor_id = d.pop('id', 0)
        sensor_state.setdefault(sensor_model, {}).setdefault(sensor_id, {})['data'] = d
        print_sensor_state()
    else:
        log.info("Unknown topic: " + msg.topic + "\t" + msg.payload.decode())


# Setup MQTT client
mqtt_client.on_connect = on_connect
mqtt_client.on_disconnect = on_disconnect
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_SERVER)
mqtt_client.loop_start()


def main():
    """MQTT Test Client"""
    log.setLevel(logging.INFO)
    log.info("MQTT RTL_433 Test Client")

    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()
