#!/usr/bin/env python3

"""Custom data handling example for rtl_433's HTTP (line) streaming API of JSON events."""

# Start rtl_433 (`rtl_433 -F http`), then this script.
# Needs the Requests package to be installed.

import requests
import json
from time import sleep

# You can run rtl_433 and this script on different machines,
# start rtl_433 with `-F http:0.0.0.0`, and change
# to e.g. `HTTP_HOST = "192.168.1.100"` (use your server ip) below.
HTTP_HOST = "127.0.0.1"
HTTP_PORT = 8433


def stream_lines():
    url = f'http://{HTTP_HOST}:{HTTP_PORT}/stream'
    headers = {'Accept': 'application/json'}

    # You will receive JSON events, one per line terminated with CRLF.
    # On Events and Stream endpoints a keep-alive of CRLF will be send every 60 seconds.
    response = requests.get(url, headers=headers, timeout=70, stream=True)
    print(f'Connected to {url}')

    for chunk in response.iter_lines():
        yield chunk


def handle_event(line):
    try:
        # Decode the message as JSON
        data = json.loads(line)

        #
        # Change for your custom handling below, this is a simple example
        #
        label = data["model"]
        if "channel" in data:
            label += ".CH" + str(data["channel"])
        elif "id" in data:
            label += ".ID" + str(data["id"])

        # E.g. match `model` and `id` to a descriptive name.
        if data["model"] == "LaCrosse-TX" and data["id"] == 123:
            label = "Living Room"

        if "battery_ok" in data:
            if data["battery_ok"] == 0:
                print(label + ' Battery empty!')

        if "temperature_C" in data:
            print(label + ' Temperature ', data["temperature_C"])

        if "humidity" in data:
            print(label + ' Humidity ', data["humidity"])

    except KeyError:
        # Ignore unknown message data and continue
        pass

    except ValueError as e:
        # Warn on decoding errors
        print(f'Event format not recognized: {e}')


def rtl_433_listen():
    """Listen to all messages in a loop forever."""

    # Loop forever
    while True:
        try:
            # Open the HTTP (line) streaming API of JSON events
            for chunk in stream_lines():
                # print(chunk)
                chunk = chunk.rstrip()
                if not chunk:
                    # filter out keep-alive empty lines
                    continue
                # Decode the JSON message
                handle_event(chunk)

        except requests.ConnectionError:
            print('Connection failed, retrying...')
            sleep(5)


if __name__ == "__main__":
    try:
        rtl_433_listen()
    except KeyboardInterrupt:
        print('\nExiting.')
        pass
