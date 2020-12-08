
# To build and run using this Dockerfile:
#
#  'docker build --tag rtl433hass:latest .'
#  'docker run -d --rm --name=rtl433hass --env MQTT_HOST=mqtt.example.net rtl433hass:latest'

FROM python:3.7

RUN apt-get update \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/* \
  && pip install paho-mqtt \
  && mkdir /etc/rtl_433

COPY rtl_433_mqtt_hass.py /usr/local/bin
COPY *.json /etc/rtl_433/

ENV MAP_FILE='/etc/rtl_433/mappings.json'

ENTRYPOINT /usr/local/bin/rtl_433_mqtt_hass.py
