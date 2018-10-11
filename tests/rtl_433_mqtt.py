#!/usr/bin/env python3
""" MQTT client for sending rtl_433 JSON data strutured into mqtt topics

Topic string is: <prefix>/<model id>/<sensor id>
Example: home/rtl_433/CALRF104/3

Recommended way of sending rtl_433 data on network is:
$ rtl_433 -F json -U | tests/rtl_433_mqtt.py

An MQTT broker e.g. 'mosquitto' must be running on local computer
"""

import json
import logging
import multiprocessing as mp
import sys

import paho.mqtt.client as mqtt

MQTT_SERVER = "127.0.0.1"
MQTT_TOPIC_PREFIX = "home/rtl_433"

#log = logging.getLogger()  # Single process logger
log = mp.log_to_stderr()  # Multiprocessing capable logger
log.setLevel(logging.INFO)

def on_connect(client, userdata, flags, rc):
    """ Callback for when the client receives a CONNACK response from the server. """
    log.info("MQTT Connection: " + mqtt.connack_string(rc))
    if rc != 0:
        log.error("Could not connect to MQTT broker. RC: " + str(rc))
        exit()


def on_disconnect(client, userdata, rc):
    if rc != 0:
        log.error("Disconnected from MQTT broker. RC: " + str(rc))


# Setup MQTT client
mqtt_client = mqtt.Client("RTL_433_MQTT")
mqtt_client.on_connect = on_connect
mqtt_client.on_disconnect = on_disconnect
mqtt_client.connect(MQTT_SERVER)
mqtt_client.loop_start()


def main():
    log.info("RTL_433 to MQTT")

    for line in sys.stdin:
        try:
            # Decode JSON payload
            d = json.loads(line)
        except json.decoder.JSONDecodeError:
            log.warning("JSON decode error: " + line)
            continue

        mqtt_topic = MQTT_TOPIC_PREFIX      # Build up topic string
        model_id = d.get('mid')
#        model_id = mid.get(d.get('model'))     # Map Model name to Model ID
        if model_id:
            mqtt_topic += '/' + model_id
            sensor_id = d.get('id')
            if sensor_id:
                mqtt_topic += '/' + str(sensor_id)

        mqtt_client.publish(mqtt_topic, line)
        log.info(mqtt_topic + "\t" + line)

"""
# Model IDs mapping the loong Model names into a short (max 8 chars) 'mid'
# Should probably be incorporated into rtl_433 sensor data itself
mid = {
    "Calibeur RF-104" :                                             "CALRF104",
    "Chuango Security Technology" :                                 "CHUANGO",
    "Danfoss CFR Thermostat" :                                      "DANCFR",
    "Fine Offset Electronics, WH2 Temperature/Humidity sensor" :    "FOWH2",
    "Fine Offset Electronics, WH25" :                               "FOWH25",
    "Fine Offset Electronics, WH0530 Temperature/Rain sensor" :     "FOWH0530",
}
"""

if __name__ == "__main__":
    main()

