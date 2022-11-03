# rtl_433 scripts (example and useful)

This directory contains a number of scripts to process the output of
rtl_433 in various ways.  Some are truly examples; they worked for
someone else and won't work for you but will help understanding.  Some
are (or will be once enhanced) actually useful to a fairly broad set
of people.

It is likely that a use outside what has been contemplated will
require writing new code, with some kind of filtering and
transformation.

Generally, python scripts should work with Python 2 or relatively
recent Python 3.  (It is TBD to deprecate Python 2; it is not clear
that anyone still cares.)

These scripts typically send data to some other system, store it in a
database, or process it in some way.  Recall that rtl_433's philosophy
is to just output received transmissions, with minimal processing and
with no checking/correlation of adjacent repeated frames.

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

There are only two pipe progams, collectd and statsd.

## UDP syslog

The `relay` examples consumes the (UDP) Syslog output from rtl_433 (or a legacy plain JSON datagram).
Basically run `rtl_433` with `-F syslog:127.0.0.1:1433` and the relay script as an unrelated process, i.e.

    rtl_433_mqtt_relay.py &
    rtl_433 -F syslog:127.0.0.1:1433

With this, one can run `tcpdump -A -i lo0 udp and port 1433`
(substitute your loopback interface) to watch the traffic.  One can
also run multiple rtl_433 processes.

# Strategies for Processing, Transmitting and Storing

(This does not belong here, but is useful to those contemplating the
scripts, so it's here pending a proper home.)

This section is speculative.

## Checksums and Repeated transmissions

Many devices will send a frame multiple times, perhaps 3 or 4, as a
form of redundancy.  Many devices have non-robust checksums.  If
displaying a temperature, that is often not a big deal.  If storing it
in a datbase, bad data is troublesome.  One could process multiple
frames that arrive close in time and try to infer which are bad
decodes and what the consensus data is, and from tha output one good
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
