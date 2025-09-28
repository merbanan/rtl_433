# rtl_433 scripts (example and useful)

This directory contains a number of scripts to process the output of
rtl_433 in various ways.  Some are truly examples; they worked for
someone else and won't work for you but will help understanding.  Some
are actually useful to a fairly broad set of people and are used in
production.  Some of course are somewhere in the middle.

It is likely that a use outside what has been contemplated will
require writing new code, with some kind of filtering and
transformation.

Generally, python scripts should work with relatively recent Python 3.
(Python 2.7 is no longer supported.)

These scripts typically send data to some other system, store it in a
database, or process it in some way.  Recall that rtl_433's philosophy
is to just output received transmissions, with minimal processing and
with no checking/correlation of adjacent repeated frames.  These
scripts bridge the gap between raw output and useful information.

This directory has a strong bias to the use of JSON; the point of that
encoding is that it is machine parseble and that's what we want to do.

At some point, generally-usable scripts should perhaps be promoted
from examples.

# Styles of Plumbing

There are two main mechanisms: piping the output json, and
syslog-style UDP with one json object per packet.

## pipe

The `pipe` examples read JSON output from `rtl_433` using a pipe, i.e.

    rtl_433 -F json ... | rtl_433_statsd_pipe.py

This is in many ways simple, but the programs must be started and
stopped together, and only one rtl_433 can write to the processing
script.

There are only two pipe programs, collectd and statsd.

## UDP syslog

The `relay` examples consumes the (UDP) Syslog output from rtl_433 (or a legacy plain JSON datagram).
Basically run `rtl_433` with `-F syslog:127.0.0.1:1433` and the relay script as an unrelated process, i.e.

    rtl_433_mqtt_relay.py &
    rtl_433 -F syslog:127.0.0.1:1433

With this, one can run `tcpdump -A -i lo0 udp and port 1433`
(substitute your loopback interface) to watch the traffic.  One can
also run multiple rtl_433 processes.

# Orientation

We attempt to categorize and describe the scripts in this directory.

Home Assistant is abbreviated HA.

## Production Scripts

A production script could be installed as a program, if it also had a
man page.  Many people should be able to use it without having to edit
the source code.

## Generally Usable Scripts

A generally usable script will likely need minor tweaking.

  - rtl_433_mqtt_relay.py: Send data via MQTT (e.g. to HA).
  - rtl_433_mqtt_hass.py: Send HA autoconfiguration data, so that entities for the decoded sensors will automatically appear.

## True Examples

These are not likely to to be useful, except that reading them will
lead to better understanding, and bits of code may be useful..  

  - mqtt_rtl_433_test_client.py: Connect to broker and print data from rtl topics
  - rtl_433_custom.php: Receive json syslog packets in php
  - rtl_433_custom.py: Receive json syslog packets in python
  - rtl_433_gps.py: Receive json data and also gpsd data 
  - rtl_433_http_cmd.py: Custom hop controller example for rtl_433's HTTP cmd API
  - rtl_433_http_cmd.sh: Custom hop controller example for rtl_433's HTTP cmd API
  - rtl_433_http_events.py: Custom data handling example for rtl_433's HTTP (chunked) streaming API of JSON events
  - rtl_433_http_stream.php: Short example of an TCP client written in PHP for rtl_433
  - rtl_433_http_stream.py: Custom data handling example for rtl_433's HTTP (line) streaming API of JSON events
  - rtl_433_http_ws.py: Custom data handling example for rtl_433's HTTP WebSocket API of JSON events

## Uncategorized

These scripts are in the directory but have not been sorted and described.

  - rtl_433_graphite_relay.py: Send data to graphite
  - rtl_433_influxdb_relay.py: Send data to influxdb
  - rtl_433_prometheus_relay.py: Send data to prometheus
  - rtl_433_rrd_relay.py: Send data to rrd
  - rtl_433_statsd_relay.py: Send data to statsd
  - rtl_433_collectd_pipe.py: Send data to collected
  - rtl_433_statsd_pipe.py: Send data to statsd
  - rtl_433_json_to_rtlwmbus.py: convert rtl_433 wmbus json output to rtlwmbus output

# Strategies for Processing, Transmitting and Storing

(This does not belong here, but is useful to those contemplating the
scripts, so it's here pending a proper home.)

This section is speculative.

## Checksums and Repeated transmissions

Many devices will send a frame multiple times, perhaps 3 or 4, as a
form of redundancy.  Many devices have non-robust checksums.  If
displaying a temperature, that is often not a big deal.  If storing it
in a database, bad data is troublesome.  One could process multiple
frames that arrive close in time and try to infer which are bad
decodes and what the consensus data is, and from that output one good
frame.  One could further reject physically implausible data (temp of
fridge has been 2C and we just got a 30C reading), but this needs to
recover from arbitrary situations so that would be tricky code to
write.  There is no code yet; this is merely a suggestion to future
hackers.

## Precision

Actual precision of devices varies, and there is unit conversion.
There should be some scheme to ensure reasonable values (vs 5 decimal
digits of temperature).  This is also for future work.

## Calibration

So far, rtl_433 does not deal with calibration; the job is to output
what was received.  A system design where the json to mqtt program has
calibration data and applies it might be sensible, but this is far from clear.

## Health monitoring

The main data is logging what was received, but it is also interesting
to know what channel rtl_433 is listening to, noise levels, that the
translation script is up, etc.   This is also for future work.
