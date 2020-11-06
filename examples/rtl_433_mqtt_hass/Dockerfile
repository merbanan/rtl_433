FROM python:3.7

RUN apt-get update \
  && apt-get clean \
  && rm -rf /var/lib/apt/lists/* \
  && pip install paho-mqtt \
  && mkdir /etc/rtl_433

COPY rtl_433_mqtt_hass.py /usr/local/bin
COPY *.json /etc/rtl_433/

ENV MAPFILE='/etc/rtl_433/example_mappings.json'

ENTRYPOINT /usr/local/bin/rtl_433_mqtt_hass.py -e
