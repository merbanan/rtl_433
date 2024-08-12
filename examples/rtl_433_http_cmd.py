#!/usr/bin/env python3

"""Custom hop controller example for rtl_433's HTTP cmd API."""

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


def set_freq(freq):
    return send_cmd({'cmd': 'center_frequency', 'val': freq})


def set_rate(rate):
    return send_cmd({'cmd': 'sample_rate', 'val': rate})


def send_cmd(params):
    url = f'http://{HTTP_HOST}:{HTTP_PORT}/cmd'
    headers = {'Accept': 'application/json'}

    # You will receive JSON events, one per line terminated with CRLF.
    # Use GET
    response = requests.get(url, params=params, headers=headers, timeout=70, stream=True)
    # or POST
    # response = requests.post(url, data=params, headers=headers, timeout=70, stream=True)
    print(f'Sending {params} to {url}')

    # Answer is lines of JSON
    return response.text


def rtl_433_control():
    """Simple timed control of rtl_433 in a loop forever."""

    # Loop forever
    while True:
        try:
            # Set first hop
            sleep(10)
            print(set_freq(433920000))
            print(set_rate(250000))

            # Set second hop
            sleep(10)
            print(set_freq(868000000))
            print(set_rate(1024000))

        except requests.ConnectionError:
            print('Connection failed, retrying in 60s...')
            sleep(60)


if __name__ == "__main__":
    try:
        rtl_433_control()
    except KeyboardInterrupt:
        print('\nExiting.')
        pass
