# rtl_433 examples
==================

Some example readers/relays for rtl_433 JSON UDP data. Should work with Python 2 and also Python 3.

Consider using PEP 3143 - Standard daemon process library or run with Supervisord.

The `pipe` examples read JSON output from `rtl_433` using a pipe, i.e.

```
rtl_433 -F json ... | rtl_433_statsd_pipe.py
````

The `relay` examples consumes the (UDP) Syslog output from rtl_433 (or a legacy plain JSON datagram).
Basically run `rtl_433` with `-F syslog:127.0.0.1:1433` and the relay script as an unreleated process, i.e.

```
rtl_433_mqtt_relay.py &
rtl_433 -F syslog:127.0.0.1:1433
````
