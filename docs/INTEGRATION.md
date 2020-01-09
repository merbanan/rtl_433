# Integration

Integration of rtl_433 output into various home automation gateways.

::: tip
If you are a user of one these systems, please help to confirm and extend the information here.
:::

## openHAB

[openHAB](https://www.openhab.org/) - open source automation software for your home

See the wiki page https://github.com/merbanan/rtl_433/wiki/How-to-integrate-rtl_433-sensors-into-openHAB-via-MQTT

Some help comes from https://community.openhab.org/t/rtl-433-to-mqtt/80652/3

Run

    rtl_433 -F "mqtt://192.168.178.42:1883,retain=0,devices=sensors/rtl_433/P[protocol]/C[channel]"

This produces some topics in the broker like this:

    sensors/rtl_433/P25/C1/id 147
    sensors/rtl_433/P25/C1/temperature_C 33.200001
    sensors/rtl_433/P25/C1/rain_mm 107.699997
    sensors/rtl_433/P25/C1/battery_ok 1
    sensors/rtl_433/P25/C1/mic CRC

You can easily set up some MQTT things then:

    Bridge mqtt:broker:My-MQTT "MQTT Broker" @ "RTL433" [
      host="192.168.x.x",
      secure=false,
      port=1883,
      qos=0,
      retain=false,
      clientid="Oh2Mqtt2Thing",
      keep_alive_time=30000,
      reconnect_time=60000
    ]
    {
        Thing topic RTL_433 "433MHz Empfänger" @ "RTL433"  {
          Channels:
              Type number : temp "Temperatur" [ stateTopic="sensors/rtl_433/P25/C1/temperature_C" ]
              Type number : hum  "Luftfeuchtigkeit" [ stateTopic="sensors/rtl_433/P25/C1/humidity" ]
              Type switch : batt "Battery schwach" [ stateTopic="sensors/rtl_433/P25/C1/battery", transformationPattern="MAP:battery.map"]
        }
    }

## Home Assistant

[Home Assistant](https://www.home-assistant.io/) - Open source home automation

Home Assistant has good MQTT support and can read rtl_433 event topics.

::: warning
Example needed
:::

See also [rtl_433_mqtt_hass.py](https://github.com/merbanan/rtl_433/tree/master/examples/rtl_433_mqtt_hass.py)
MQTT Home Assistant auto discovery.

## Domoticz

[Domoticz](http://www.domoticz.com/) - Home Automation System

Domoticz has built-in support for reading from rtl_433 using pipes.

There is also a newer plugin using MQTT: [enesbcs/pyrtl433](https://github.com/enesbcs/pyrtl433).

::: warning
Testing and example needed
:::

# NodeRED

[NodeRED](https://nodered.org/) - Flow-based programming for the Internet of Things

Node RED has built-in support for reading from MQTT and thus rtl_433 events.

::: warning
Example needed
:::

## Databases

You likely need to filter and transform rtl_433's output before sending it to a database.
It's recommended you read the JSON data and process it to your specific requirements.

Some example pipes/relays for rtl_433 JSON data. Should work with Python 2 and also Python 3.

The `pipe` examples read JSON output from `rtl_433` using a pipe, i.e.

    rtl_433 -F json ... | rtl_433_statsd_pipe.py

The `relay` examples consumes the (UDP) Syslog output from rtl_433 (or a legacy plain JSON datagram).
Basically run `rtl_433` with `-F syslog:127.0.0.1:1433` and the relay script as an unrelated process, i.e.

    rtl_433_mqtt_relay.py &
    rtl_433 -F syslog:127.0.0.1:1433

### RRD

See [rtl_433_rrd_relay.py](https://github.com/merbanan/rtl_433/tree/master/examples/rtl_433_rrd_relay.py)

### Statsd

See [rtl_433_statsd_pipe.py](https://github.com/merbanan/rtl_433/tree/master/examples/rtl_433_statsd_pipe.py)
See [rtl_433_statsd_relay.py](https://github.com/merbanan/rtl_433/tree/master/examples/rtl_433_statsd_relay.py)

### Collectd

See [rtl_433_collectd_pipe.py](https://github.com/merbanan/rtl_433/tree/master/examples/rtl_433_collectd_pipe.py)

### Graphite

See [rtl_433_graphite_relay.py](https://github.com/merbanan/rtl_433/tree/master/examples/rtl_433_graphite_relay.py)

### InfluxDB

There is built-in support for an InfluxDB output.

Specify an InfluxDB 2.0 server with e.g.

    rtl_433 -F "influx://localhost:9999/api/v2/write?org=<org>&bucket=<bucket>,token=<authtoken>"

Specify an InfluxDB 1.x server with e.g.

    rtl_433 -F "influx://localhost:8086/write?db=<db>&p=<password>&u=<user>"

It is recommended to additionally use the option `-M time:unix:usec:utc` for correct timestamps in InfluxDB.

If you want to filter messages before they are inserted into the InfluxDB or if you want to transform the data
see [rtl_433_influxdb_relay.py](https://github.com/merbanan/rtl_433/tree/master/examples/rtl_433_influxdb_relay.py)
for an example script.

The [rtl433_influx](https://github.com/azrdev/rtl433_influx/) project allows to dump the JSON output of rtl_433 into InfluxDB.

InfluxDB also comes with MQTT integration through Telegraf,
see [MQTT Monitoring](https://www.influxdata.com/integration/mqtt-monitoring/)
and [MQTT Consumer Input Plugin](https://github.com/influxdata/telegraf/tree/master/plugins/inputs/mqtt_consumer).

### MySQL

TBD.

### Sqlite

TBD.
