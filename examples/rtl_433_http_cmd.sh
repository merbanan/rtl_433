#!/bin/sh

# Custom hop controller example for rtl_433's HTTP cmd API.

# Start rtl_433 (`rtl_433 -F http`), then this script.
# Needs the xh tool installed (or httpie and change `xh` to `http`)

while : ; do
  sleep 10
  xh :8433/cmd cmd==center_frequency val==433920000
  xh :8433/cmd cmd==sample_rate val==250000
  sleep 10
  xh :8433/cmd cmd==center_frequency val==868000000
  xh :8433/cmd cmd==sample_rate val==1024000
done
