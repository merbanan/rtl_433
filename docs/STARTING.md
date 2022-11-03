# Getting Started

A short summary how to operate `rtl_433`.

## Options

Add options to the `rtl_433` command line invocation to specify the mode of operation.

E.g. the option `-V` will output the version string and exit,
the option `-h` will output a brief usage help and exit.

Some options take an argument, and you can also use those without argument or `help` or `?` to get brief usage instructions.
E.g. `-d`, `-g`, `-R`, `-X`, `-F`, `-M`, `-r`, `-w`, or `-W` without argument will list the argument syntax.

Command line options a parsed left to right and will override each other or stack in some cases (frequency hopping).

E.g. try the option `-V -h` to output the version string and exit, the `-h` option will not be reached,
the other way around `-h -V` you will see the help output but no version string afterwards (but the help includes the version info).

This ordering is important to keep in mind, generally go "inputs", "processing options", "outputs".

::: tip
    [-V] Output the version string and exit
    [-h] Output this usage help and exit
         Use -d, -g, -R, -X, -F, -M, -r, -w, or -W without argument for more help
:::

## Configuration files

You can also use a configuration file to give the same options.
Files will be read in order and options given will also override in that order.
Configuration files can be mixed with command line options.

You can instruct `rtl_433` to read a configuration file with the `-c <path>` option.

By default a configuration file will be searched for and loaded from
- `rtl_433.conf` at the current directory
- `$HOME/.config/rtl_433/rtl_433.conf`
- `/usr/local/etc/rtl_433/rtl_433.conf`
- `/etc/rtl_433/rtl_433.conf`

An example configuration file with information on all possible options is provided at [rtl_433.example.conf](https://github.com/merbanan/rtl_433/blob/master/conf/rtl_433.example.conf).

::: tip
    [-c <path>] Read config options from a file
:::

## Select an input

`rtl_433` can read live inputs (SDR hardware and network streams), sample files, and test codes.

Choose a live input with `-d`:
- `-d <RTL-SDR USB device index>` e.g. `-d 0` for the first RTL-SDR found,
- `-d :<RTL-SDR USB device serial>` e.g. `-d :NESDRSMA` (set the serial using the `rtl_eeprom` tool)
- `-d <SoapySDR device query>` e.g. `-d driver=lime`
- `-d rtl_tcp` e.g. `-d rtl_tcp://192.168.1.2:1234`

The default is to use the first RTL-SDR available (`-d 0`).
You can switch that to using the first SoapySDR available by using `-d ""`, i.e. the empty SoapySDR search string.

::: warning
When running multiple instances of `rtl_433` be sure to use a distinct input for each, do not rely on the auto-selection of the first available input.
:::

Choose a file input using `-r` e.g. `-r g001_433.92M_250k.cu8`
If you list files to read as last options then you can omit the `-r` e.g. `rtl_433 g001_433.92M_250k.cu8`

If you are testing a decoder you can list a demodulated bit pattern as input using the `-y` option, e.g. `-y "{25}fb2dd58"`

::: tip
    [-d <RTL-SDR USB device index> | :<RTL-SDR USB device serial> | <SoapySDR device query> | rtl_tcp | help]
    [-r <filename> | help] Read data from input file instead of a receiver
    [-y <code>] Verify decoding of demodulated test data (e.g. "{25}fb2dd58") with enabled devices
:::

## Configure the input

Live inputs (from SDR hardware) need some settings to work, usually you at least want to specify the center frequency.

The default center frequency is `433.92M`, select a frequency using `-f <frequency>`.
Suffixes of `M`, and `k`, `G` are accepted.

Multiple center frequencies can be given to set up frequency hopping.
The hopping time can be given with `-H <seconds>`, the default is 10 minutes (600 s).
Multiple hopping times can be given and apply to each frequency given in that order.
You can give `-E hop` to hop immediatly after each received event.

The default sample rate for `433.92M` is `250k` Hz and `1000k` for higher frequencies like `868M`.
Select a sample rate using `-s <sample rate>` -- rates higher than `1024k` or maybe `2048k` are not recommended.

Specific settings for an SDR device can be given with `-g <gain>`, `-p <ppm_error>`,
and even `-t <settings>` to apply a list of keyword=value settings for SoapySDR devices.

::: tip
    [-f <frequency>] Receive frequency(s) (default: 433920000 Hz)
    [-H <seconds>] Hop interval for polling of multiple frequencies (default: 600 seconds)
    [-E hop | quit] Hop/Quit after outputting successful event(s)
    [-s <sample rate>] Set sample rate (default: 250000 Hz)
    [-g <gain> | help] (default: auto)
    [-t <settings>] apply a list of keyword=value settings for SoapySDR devices
         e.g. -t "antenna=A,bandwidth=4.5M,rfnotch_ctrl=false"
    [-p <ppm_error>] Correct rtl-sdr tuner frequency offset error (default: 0)
:::

## Verbose output

If `rtl_433` seems to "hang", it's usually just not receiving any signals that can be successfully decoded.
The default is to be silent until there is a solid data reception.

Instruct `rtl_433` not to be silent, use:
-  `-v` to show detailed notes on startup,
-  `-vv` to show failed decoding attempts,
-  `-vvv` to show all decoding attempts,
-  `-A` to analyze every signal in detail.

::: tip
Disable all decoders with `-R 0` if you want analyzer output only.
:::

Alternatively get periodic status output using: `-M level` `-M noise` `-M stats:2:30`

::: tip
    [-v] Increase verbosity (can be used multiple times).
         -v : verbose, -vv : verbose decoders, -vvv : debug decoders, -vvvv : trace decoding).
    [-A] Pulse Analyzer. Enable pulse analysis and decode attempt.
:::

## Select outputs

The default output of `rtl_433`, if no outputs are selected, is to the screen.
Any number of outputs can be selected:
- `-F kv` prints to the screen
- `-F json` prints json lines
- `-F csv` prints a csv formatted file
- `-F mqtt` sends to MQTT
- `-F influx` sends to InfluxDB
- `-F syslog` send UDP messages
- `-F trigger` puts a `1` to the given file, can be used to e.g. on a Raspberyy Pi flash the LED.

Append output to file with `:<filename>` (e.g. `-F csv:log.csv`), default is to print to stdout.
Specify host/port for `mqtt`, `influx`, `syslog`, with e.g. `-F syslog:127.0.0.1:1514`

::: tip
    [-F kv | json | csv | mqtt | influx | syslog | trigger | null | help] Produce decoded output in given format.
:::

## Write outputs to files

You can write all received raw data to a file with `-w <filename>` (or  `-W <filename>` to overwrite an existing file).

::: tip
    [-w <filename> | help] Save data stream to output file (a '-' dumps samples to stdout)
    [-W <filename> | help] Save data stream to output file, overwrite existing file
:::

## Store raw sample data

`rtl_433` can write a file for each received signal.
This is the preferred mode for generating files to later analyze or add as test cases.
Use
- `-S all` to write all signals to files,
- `-S unknown` to write signals which couldn't be decoded to files,
- `-S known` to write signals that could be decoded to files.

The saves signals are raw I/Q samples (uint8 pcm, 2 channel).

::: tip
    [-S none | all | unknown | known] Signal auto save. Creates one file per signal.
:::

## Select decoders

The `-R` option selects decoders to use. The option can be given multiple times.
Default is to activate all available decoders which are not default-disabled due to known problems.
You can disable some decoders using negative number, e.g. `-R -3`.
You can enable only select decoders by using some `-R` options, e.g. `-R 3`.
You can disable all decoders using some `-R 0`.

Additional flexible general purpose decoders can be added using `-X <spec>`.

::: tip
Disable all decoders with `-R 0` if you want only the given flex decoder.
:::

::: tip
    [-R <device> | help] Enable only the specified device decoding protocol (can be used multiple times)
         Specify a negative number to disable a device decoding protocol (can be used multiple times)
    [-X <spec> | help] Add a general purpose decoder (prepend -R 0 to disable all decoders)
:::

## Demodulator options

The operation of the demodulator stage can be tuned with the `-Y` option.

For the `433.92M` frequency the `classic` pulse detector is used by default,
for higher frequencies like `868M` the `minmax` pulse detector is used by default.

Use `-Y classic` or `-Y minmax` to force the use of a FSK pulse detector.

Use `-Y autolevel` to automatically adjust the minimum detection level based on average estimated noise. Recommended.

Use `-Y squelch` to skip frames below estimated noise level to reduce cpu load. Recommended.

::: tip
    [-Y auto | classic | minmax] FSK pulse detector mode.
    [-Y level=<dB level>] Manual detection level used to determine pulses (-1.0 to -30.0) (0=auto).
    [-Y minlevel=<dB level>] Manual minimum detection level used to determine pulses (-1.0 to -99.0).
    [-Y minsnr=<dB level>] Minimum SNR to determine pulses (1.0 to 99.0).
    [-Y autolevel] Set minlevel automatically based on average estimated noise.
    [-Y squelch] Skip frames below estimated noise level to reduce cpu load.
    [-Y ampest | magest] Choose amplitude or magnitude level estimator.
:::

## Meta-data and data conversion

Additional meta data can be added to the output using the `-M option`.
E.g. use `-M level` to add Modulation, Frequency, RSSI, SNR, and Noise meta data.

Meta data formats can be selected, e.g. use `-M time:iso:utc:usec` to use the ISO format in the UTC zone with added microseconds.

Various tags can be added to all event outputs. Use
- `-K FILE` Add the expanded name of the input file to every output line,
- `-K PATH` Add the expanded path of the input file to every output line,
- `-K <tag>` Add an expanded token or fixed tag to every output line.
- `-K <key>=<tag>` Add an expanded token or fixed tag to every output line.

Known data units can be converted to SI units or Customary (US) units.
The default is to output native units as received.
Use
- `-C native` Do not convert units in decoded output.
- `-C si` Convert units to SI in decoded output.
- `-C customary` Convert units to Customary (US) in decoded output.

::: tip
    [-M time[:<options>] | protocol | level | noise[:<secs>] | stats | bits] Add various metadata to every output line.
      Use "time" to add current date and time meta data (preset for live inputs).
      Use "time:rel" to add sample position meta data (preset for read-file and stdin).
      Use "time:unix" to show the seconds since unix epoch as time meta data.
      Use "time:iso" to show the time with ISO-8601 format (YYYY-MM-DD"T"hh:mm:ss).
      Use "time:off" to remove time meta data.
      Use "time:usec" to add microseconds to date time meta data.
      Use "time:tz" to output time with timezone offset.
      Use "time:utc" to output time in UTC.
          (this may also be accomplished by invocation with TZ environment variable set).
          "usec" and "utc" can be combined with other options, eg. "time:unix:utc:usec".
      Use "protocol" / "noprotocol" to output the decoder protocol number meta data.
      Use "level" to add Modulation, Frequency, RSSI, SNR, and Noise meta data.
      Use "noise[:secs]" to report estimated noise level at intervals (default: 10 seconds).
      Use "stats[:[<level>][:<interval>]]" to report statistics (default: 600 seconds).
        level 0: no report, 1: report successful devices, 2: report active devices, 3: report all

    [-K FILE | PATH | <tag> | <key>=<tag>] Add an expanded token or fixed tag to every output line.
      If <tag> is "FILE" or "PATH" an expanded token will be added.
      The <tag> can also be a GPSd URL, e.g.
          "-K gpsd,lat,lon" (report lat and lon keys from local gpsd)
          "-K loc=gpsd,lat,lon" (report lat and lon in loc object)
          "-K gpsd" (full json TPV report, in default "gps" object)
          "-K foo=gpsd://127.0.0.1:2947" (with key and address)
          "-K bar=gpsd,nmea" (NMEA default GPGGA report)
          "-K rmc=gpsd,nmea,filter='$GPRMC'" (NMEA GPRMC report)
      Also <tag> can be a generic tcp address, e.g.
          "-K foo=tcp:localhost:4000" (read lines as TCP client)
          "-K bar=tcp://127.0.0.1:3000,init='subscribe tags\r\n'"
          "-K baz=tcp://127.0.0.1:5000,filter='a prefix to match'"

    [-C native | si | customary] Convert units in decoded output.
:::

## Mode of operation

When reading live inputs `rtl_433` will usually run forever, but you can limit the runtime
- to a specific time using `-T <seconds>`, also formats like `12:34` or `1h23m45s` are accepted,
- to a number of samples using `-n <value>` as a number of samples to take (each sample is an I/Q pair),
- to recieving an event using `-E quit`, to quit after outputting the first event.

When reading input from files `rtl_433` will process the data as fast as possible.
You can limit the processing to original (or N-times) real-time using `-M replay[:N]`.

::: tip
    [-n <value>] Specify number of samples to take (each sample is an I/Q pair)
    [-T <seconds>] Specify number of seconds to run, also 12:34 or 1h23m45s
    [-E hop | quit] Hop/Quit after outputting successful event(s)
    [-M replay[:N]] to replay file inputs at (N-times) realtime.
:::