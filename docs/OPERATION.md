# Basic rtl_433 operation

A startup rtl_433 will read config files and parse command line arguments, then it will loop through these steps:

- Inputs
- Loaders
- Processing
- Analysis
- Decoders
- Dumper
- Outputs

rtl_433 will either acquire a live signal from an input or read a sample file with a loader.
Then process that signal, analyse it's properties (if enabled) and write the signal with dumpers (if enabled).
The raw data is run through decoders to produce decoded output data.

## Inputs

Possible inputs are RTL-SDR, SoapySDR, and rtl_tcp.

Inputs are selected with the `-d` option:
```
  [-d <RTL-SDR USB device index> | :<RTL-SDR USB device serial> | <SoapySDR device query> | rtl_tcp | help]
```

### RTL-SDR

For RTL-SDR use the `-d` option as:

```
  [-d <RTL-SDR USB device index>] (default: 0)
  [-d :<RTL-SDR USB device serial (can be set with rtl_eeprom -s)>]
```

If RTL-SDR support is compiled in (see the first line of `rtl_433 -V`) the default input will be the first available RTL-SDR device.
This can also explicitly be selected with `rtl_433 -d 0`. Use e.g. `rtl_433 -d 1` to select the second device.

If you have set a serial number on your device you can use that number prefixed with a colon to select a device,
e.g. `rtl_433 -d :NESDRSMA`.

The sample format read from RTL-SDR is always `CU8`.

### SoapySDR

For SoapySDR use the `-d` option as:

```
  [-d ""] Open default SoapySDR device
  [-d driver=rtlsdr] Open e.g. specific SoapySDR device
```

If SoapySDR support is compiled in (see the first line of `rtl_433 -V`) and RTL-SDR is not then the default input will be the first available SoapySDR device.
This can also explicitly be selected with `rtl_433 -d ""`.

Otherwise specify a driver string to select the SoapySDR device. Use e.g. `rtl_433 -d "driver=rtlsdr"` to use RTL-SDR over Soapy.

Usual SoapySDR driver string are e.g. `"driver=remote,remote=tcp://192.168.2.1:55132"`, `"driver=plutosdr"`, etc.

The sample format read from SoapySDR is likely `CS16`.
A sample format of `CU8` is tried first, but unlikely to be supported by SoapySDR drivers.

### rtl_tcp

For rtl_tcp use the `-d` option as:

```
  [-d rtl_tcp[:[//]host[:port]] (default: localhost:1234)
    Specify host/port to connect to with e.g. -d rtl_tcp:127.0.0.1:1234
```

The rtl_tcp input is always available. The default host is "localhost" and default port is "1234".

Use e.g. `rtl_433 -d rtl_tcp:192.168.2.1` or `rtl_433 -d rtl_tcp:192.168.2.1:2143` to select a specific source.

### Input Gain

The input device gain can be set with the `-g` option:

```
  [-g <gain>] (default: auto)
    For RTL-SDR: gain in dB ("0" is auto).
    For SoapySDR: gain in dB for automatic distribution ("" is auto), or string of gain elements.
    E.g. "LNA=20,TIA=8,PGA=2" for LimeSDR.

```

The default gain setting will be automatic gain (AGC enabled).

For RTL-SDR the gain is given in dB, where "0" selects automatic gain.

For SoapySDR a gain argument of `""` selects automatic gain,
a gain value in dB can be used for automatic distribution to the gain stages,
and string of gain elements sets the given gain stages individually.

Use e.g. `-g "LNA=20,TIA=8,PGA=2"` for LimeSDR.

### Antenna and settings

For SoapySDR the antenna and various other settings can be selected with `-t`:

```
  [-t <settings>] apply a list of keyword=value settings for SoapySDR devices
       e.g. -t "antenna=A,bandwidth=4.5M,rfnotch_ctrl=false"
```

### Center Frequency

The center frequency can be selected with `-f`:

```
  [-f <frequency>] Receive frequency(s) (default: 433920000 Hz)
```

The default frequency is 433.92 MHz and can be explicitly requested with `-f 433.92M`.

You can give a frequency in Hz, like `-f 433920000` or use suffixes of `k`, `M`, or `G`,
e.g. `-f 433920k`, or `-f 433.92M`.

Other interesting frequencies are e.g. `-f 868M`, `-f 315M`, `-f 345M`, `-f 915M`.
If you fine tune the frequency to your sender device you should avoid hitting the sender frequency dead center.
The resulting DC (direct current) signal is often attenuated by receivers and hard to make out when analysing samples.
A small offset of 10 kHz to 50 kHz works best.

The `-f` option can be used multiple times to set up a list of frequency to hop.
Use the `-H` option to set up the time to stay on each frequency or list on `-H` per `-f` to set a stay time for each frequency.
(The last hop time given will be the default for all frequencies.)

### PPM correction

A PPM error correction value can be given with `-p`:

```
  [-p <ppm_error] Correct rtl-sdr tuner frequency offset error (default: 0)
```

The PPM error correction is most commonly used to counter the drift in warmed up RTL-SDR devices.

### Sample rate

A sample rate value can be given with `-s`:

```
  [-s <sample rate>] Set sample rate (default: 250000 Hz)
```

The default sample rate is 250 kHz and can be explicitly requested with `-s 250k`.

You can give a sample rate in Hz, like `-s 250000` or use suffixes of `k`, `M`, or `G`,
e.g. `-f 250k`, or `-f 8M`.
Note that the suffix is metric, the 1024000 Hz sample rate common with RTL-SDR has to be given as `-s 1024k`.

## Decoders

Decoders can be selected with the `-R`, `-G`, and `-X` option:

```
  [-R <device> | help] Enable only the specified device decoding protocol (can be used multiple times)
       Specify a negative number to disable a device decoding protocol (can be used multiple times)
  [-G] Enable blacklisted device decoding protocols, for testing only.
  [-X <spec> | help] Add a general purpose decoder (prepend -R 0 to disable all decoders)
```

By default all non-blacklisted decoders are enabled.

You can disable selected decoders with any number of `-R -<number>` options.
E.g. use `rtl_433 -R -8 -19` to disable the LaCrosse and Nexus decoders.

Some decoders have little validity checking and may share very common signal characteristics.
This will result in lots of false-positive decodes.
These decoders are black-listed and you need to explicitly enable them with `-R <number>`.

You can also use `-G` to enable all the blacklisted decoders.
This is for testing only and strongly discouraged for continuous operation.

You can enable only selected decoders with any number of `-R <number>` options.
Note that this will override the default and not select any decoder by default.
E.g. use `rtl_433 -R 8 19` to enable only the LaCrosse and Nexus decoders.

An output line of `Registered <n> out of <N> device decoding protocols` will tersely show the enabled decoders.

Lastly the `-X` option can be used to add a custom flex decoder.
This can be used with `-R 0` to disable all default decoders.
E.g. `rtl_433 -R 0 -X "<spec>"` will only run your given custom decoder.

## Flex Decoder

A flexible general purpose decoder can be added with the `-X` option:

```
  [-X <spec>] to add a flexible general purpose decoder.
      <spec> is "key=value[,key=value...]"
```
Most common keys are:
- `name=<name>` (or: `n=<name>`)
- `modulation=<modulation>` (or: `m=<modulation>`)
- `short=<short>` (or: `s=<short>`)
- `long=<long>` (or: `l=<long>`)
- `sync=<sync>` (or: `y=<sync>`)
- `reset=<reset>` (or: `r=<reset>`)
- `gap=<gap>` (or: `g=<gap>`)
- `tolerance=<tolerance>` (or: `t=<tolerance>`)

where:
`<name>` can be any descriptive name tag you need in the output.

`<modulation>` is one of:
- `OOK_MC_ZEROBIT` :  Manchester Code with fixed leading zero bit
- `OOK_PCM` :         Pulse Code Modulation (RZ or NRZ)
- `OOK_PPM` :         Pulse Position Modulation
- `OOK_PWM` :         Pulse Width Modulation
- `OOK_DMC` :         Differential Manchester Code
- `OOK_PIWM_RAW` :    Raw Pulse Interval and Width Modulation
- `OOK_PIWM_DC` :     Differential Pulse Interval and Width Modulation
- `OOK_MC_OSV1` :     Manchester Code for OSv1 devices
- `FSK_PCM` :         FSK Pulse Code Modulation
- `FSK_PWM` :         FSK Pulse Width Modulation
- `FSK_MC_ZEROBIT` :  Manchester Code with fixed leading zero bit

`<short>`, `<long>`, `<sync>` are nominal modulation timings in us,
`<reset>`, `<gap>`, `<tolerance>` are maximum modulation timings in us:

- PCM
  - `short`: Nominal width of pulse [us]
  - `long`: Nominal width of bit period [us]
- PPM
  - `short`: Nominal width of `0` gap [us]
  - `long`: Nominal width of `1` gap [us]
- PWM
  - `short`: Nominal width of `1` pulse [us]
  - `long`: Nominal width of `0` pulse [us]
  - `sync`: Nominal width of sync pulse [us] (optional)
- common
  - `gap`: Maximum gap size before new row of bits [us]
  - `reset`: Maximum gap size before End Of Message [us]
  - `tolerance`: Maximum pulse deviation [us] (optional).

Additional options are:
- `bits=<n>` : only match if at least one row has `<n>` bits
- `rows=<n>` : only match if there are `<n>` rows
- `repeats=<n>` : only match if some row is repeated `<n>` times.
  - use `opt>=n` to match at least `<n>` and `opt<=n` to match at most `<n>`
- `invert` : invert all bits
- `reflect` : reflect each byte (MSB first to MSB last)
- `match=<bits>` : only match if the `<bits>` are found
- `preamble=<bits>` : match and align at the `<bits>` preamble.
  - `<bits>` is a row spec of `{<bit count>}<bits as hex number>`
- `unique` : suppress duplicate row output
- `countonly` : suppress detailed row output

E.g. `-X "n=doorbell,m=OOK_PWM,s=400,l=800,r=7000,g=1000,match={24}0xa9878c,repeats>=3"` specifies:

- `name` is doorbell
- `modulation` is `OOK_PWM`
- width of a `short` bit is 400 µs
- width of a `long` bit is 800 µs
- maximum gap width to `reset` is 7000 µs
- maximum `gap` width to new row is 1000 µs
- the data needs to contain the `match` of 24 bits `0xa9878c`
- the data needs to `repeat` at least 3 times

See the [`conf`](https://github.com/merbanan/rtl_433/tree/master/conf) folder for some examples of flex specs.

## Analysis

Signal data can be analysed with `-A`, `-a`, sample data can be dumped with `-S`:

```
  [-a] Analyze mode. Print a textual description of the signal.
  [-A] Pulse Analyzer. Enable pulse analysis and decode attempt.
       Disable all decoders with -R 0 if you want analyzer output only.
  [-S none | all | unknown | known] Signal auto save. Creates one file per signal.
       Note: Saves raw I/Q samples (uint8 pcm, 2 channel). Preferred mode for generating test files.
```

The `-a` option enables the (old) pulse decoder to print a textual description of the signal.
The output might not be too useful, best to use the newer `-A` option.

The `-A` option enables the (new) pulse analyzer.
Each received transmission will be displayed in a statistical overview.
A probable coding will be infered and attempted to decode.

The "Pulse width distribution", "Gap width distribution", and "Pulse period distribution"
can tell you about the timing in the `width` column,
and the coding in the `count` column.

E.g. a single (or dominant count) pulse width with two gap widths is likely PPM,
E.g. a two (or dominant count) pulse widths with a sinle gap widths or single period width is likely PWM.

Disable all decoders with `-R 0` if you want to view the analyzer output only.

The `-S` option allows you to dump received transmissions for further analysis.
Use e.g. `rtl_433 -S all` to dump all signals or `rtl_433 -S unknown` to dump only signals with no successful decodes (by enabled decoders).

On file will be created per signal, see also "File names".
Note: Saves raw I/Q samples `CU8` (uint8 pcm, 2 channel) for RTL-SDR and `CS16` (int16 pcm, 2 channel) for SoapySDR.

## Loaders and Dumpers

Sample data can be loaded or dumped with `-r`, `-w`, `-W`, and codes verified with `-y`:

```
  [-r <filename> | help] Read data from input file instead of a receiver
  [-w <filename> | help] Save data stream to output file (a `-` dumps samples to stdout)
  [-W <filename> | help] Save data stream to output file, overwrite existing file
  [-y <code>] Verify decoding of demodulated test data (e.g. "{25}fb2dd58") with enabled devices
```

### Read file (loaders)

Use the `-r` option or stdin to read signal data (instead of live input):

```
  [-r <filename> | help] Read data from input file instead of a receiver
```

Parameters are detected from the full path, file name, and extension. See also "File names".

File content and format options are:
`cu8`, `cs16`, `cf32` (`IQ` implied), and `am.s16`.

### Write file (dumpers)

Use the `-w` and `-W` option to dump all signal data:

```
  [-w <filename>] Save data stream to output file (a `-` dumps samples to stdout)
  [-W <filename>] Save data stream to output file, overwrite existing file
```

Parameters are detected from the full path, file name, and extension. See also "File names".

File content and format options are:
`cu8`, `cs16`, `cf32` (`IQ` implied),
`am.s16`, `am.f32`, `fm.s16`, `fm.f32`,
`i.f32`, `q.f32`, `logic.u8`, `ook`, and `vcd`.

For example you can dump the live decoded pulse data to stdout with `rtl_433 -w OOK:-`.

### Load bitbuffer code

Use the `-y` option to test a known code line (bitbuffer):

```
  [-y <code>] Verify decoding of demodulated test data (e.g. "{25}fb2dd58") with enabled devices
```

If you are developing or testing a decoder you can skip the device input or sample loading step and directly give a known code line (bitbuffer) to the enabled decoders.

### File names

Samples recorded using the `-S` option will automaticly be given filenames with some meta-data.
The signals will be stored individually in files named `g<NNN>_<FFF>M_<RRR>k.cu8` :

| Parameter | Description
|---------|------------
| **NNN** | signal grabbed number
| **FFF** | frequency
| **RRR** | sample rate

File names used with `-r`, and `-w` / `-W` (loaders and dumpers) also follow that convention.

A center frequency is detected from the filename as (fractional) number suffixed with `M`, `Hz`, `kHz`, `MHz`, or `GHz`.

A sample rate is detected from the filename as (fractional) number suffixed with `k`, `sps`, `ksps`, `Msps`, or `Gsps`.

Parameters must be separated by non-alphanumeric chars and are case-insensitive.

File content and format are detected by th extension, possible options are:

- `cu8` (`IQ` implied)
- `cs16` (`IQ` implied)
- `cf32` (`IQ` implied)
- `am.s16'`
- `am.f32`
- `fm.s16`
- `fm.f32`
- `i.f32`
- `q.f32`
- `logic.u8`
- `ook`
- `vcd`

Overrides can be prefixed to the actual filename, separated by colon (`:`).
E.g. default detection by extension: path/filename.am.s16 and forced overrides: am:s16:path/filename.ext

::: warning
Note that not all file types are supported/applicable by loaders or dumpers.
:::

## Outputs

Use the `-F` option to add outputs, use `-M`, `-K`, and `-C` to configure meta-data:

```
  [-F kv | json | csv | mqtt | syslog | null | help] Produce decoded output in given format.
       Append output to file with :<filename> (e.g. -F csv:log.csv), defaults to stdout.
       Specify host/port for syslog with e.g. -F syslog:127.0.0.1:1514
  [-M time[:<options>] | protocol | level | stats | bits | help] Add various meta data to each output.
  [-K FILE | PATH | <tag>] Add an expanded token or fixed tag to every output line.
  [-C native | si | customary] Convert units in decoded output.
```

Without any `-F` option the default is KV output. Use `-F null` to remove that default.

### KV output

Use `-F kv` to add an output in KV format.

A colorful, column based output intended for screen display.

Append output to file with `:<filename>` (e.g. `-F kv:log.txt`), defaults to stdout.

::: warning
Note: the `kv` output is not a machine-readable key-value format, use the JSON output for that.
:::

### JSON output

Use `-F json` to add an output in JSON format.

Universally machine-readable output.

Append output to file with `:<filename>` (e.g. `-F json:log.json`), defaults to stdout.

### CSV output

Use `-F csv` to add an output in CSV format.

Append output to file with `:<filename>` (e.g. `-F csv:log.csv`), defaults to stdout.

::: warning
Note: the `csv` output is not recommended for post-processing, use the JSON output for a machine-readable format.
:::

### MQTT output

Use `-F mqtt` to add an output in MQTT format.

Specify MQTT server with e.g. `-F mqtt://localhost:1883`.

Add MQTT options with e.g. `-F "mqtt://host:1883,opt=arg"`.
Supported MQTT options are: `user=foo`, `pass=bar`, `retain[=0|1]`, `<format>[=<topic>]`.

Supported MQTT formats: (default is all formats)
- `events`: posts JSON event data
- `states`: posts JSON state data
- `devices`: posts device and sensor info in nested topics

The `<topic>` string will expand keys like `[/model]`, see below.
E.g. `-F "mqtt://localhost:1883,user=USERNAME,pass=PASSWORD,retain=0,devices=rtl_433[/id]"`

### MQTT Format Strings

Use format strings of:

- `[token]`: expand to token or nothing
- `[token:default]` expand to token or default
- `[/token]` expand to token with leading slash or nothing
- `[/token:default]` expand to token or default with leading slash

Tokens are `type`, `model`, `subtype`, `channel`, `id`, and `protocol` for now.

Note that for `protocol` to be available you first need to add it to the meta-data with `-M protocol`.

Examples:

- `sensors[/channel:0][/id]` : always have a channel add id if available, you can also use `sensors/[channel:0][/id]`
- `sensors[/channel][/id]` : use channel and then id, each if available
- `sensors[/id][/channel]` : use id and then channel, each if available
- `sensors[/channel:0]-[id:0]` : always have a combined channel and id
- ...

Defaults are a base topic of `rtl_433/<hostname>/` continued
- for `devices` with `devices[/type][/model][/subtype][/channel][/id]`
- for `events` with `events`
- for `states` with `states`

### SYSLOG output

Use `-F syslog` to add an output in SYSLOG format.

Specify host/port for syslog with e.g. `-F syslog:127.0.0.1:1514`

A UDP output of JSON messages with Syslog compatible header data.

E.g. a UDP text payload of
```
<165>1 2019-08-29T06:38:19Z raspi.fritz.box rtl_433 - - - {"time":"2019-08-29 08:38:19","model":"Nexus-TH","id":42,"channel":2,"battery_ok":1,"temperature_C":20.5,"humidity":83}
```
See also [RFC 5424 - The Syslog Protocol](https://tools.ietf.org/html/rfc5424#page-8)

### NULL output

Without any `-F` option the default is KV output. Use `-F null` to remove that default.

### Meta information

```
  [-M time[:<options>]|protocol|level|stats|bits|oldmodel]
    Add various metadata to every output line.
```
- Use `time` to add current date and time meta data (preset for live inputs).
- Use `time:rel` to add sample position meta data (preset for read-file and stdin).
- Use `time:unix` to show the seconds since unix epoch as time meta data.
- Use `time:iso` to show the time with ISO-8601 format (`YYYY-MM-DD"T"hh:mm:ss`).
- Use `time:off` to remove time meta data.
- Use `time:usec` to add microseconds to date time meta data.
- Use `time:utc` to output time in UTC.
  (this may also be accomplished by invocation with TZ environment variable set).
  `usec` and `utc` can be combined with other options, eg. `time:unix:utc:usec`.
- Use `protocol` / `noprotocol` to output the decoder protocol number meta data.
- Use `level` to add Modulation, Frequency, RSSI, SNR, and Noise meta data.
- Use `stats[:[<level>][:<interval>]]` to report statistics (default: 600 seconds).
  level 0: no report, 1: report successful devices, 2: report active devices, 3: report all
- Use `bits` to add bit representation to code outputs (for debug).

```
  [-K FILE | PATH | <tag>] Add an expanded token or fixed tag to every output line.
```

- Use `-K FILE` to add the base file name (from a loader) to every output line.
- Use `-K PATH` to add the full path name (from a loader) to every output line.
- Use `-K <tag>` to add a fixed custom tag to every output line.

### Data conversion

You can choose to normalize data by unit conversion with the `-C` option:

```
  [-C native | si | customary] Convert units in decoded output.
```

The default is no conversion, you explicitly select this with `-C native`.

With `-C si` units are converted to the SI system:
- converts fields of Fahrenheit to Celsius (`_F` to `_C`)
- converts fields of Miles/h to km/h (`_mph` to `_kph`, `_mi_h` to `_km_h`)
- converts fields of Inch to mm (`_in` to `_mm`)
- converts fields of Inch/h to mm/h (`_in_h` to `_mm_h`)
- converts fields of InchHg to hPa (`_inHg` to `_hPa`)
- converts fields of PSI to kPa (`_PSI` to `_kPa`)

With `-C customary` units are converted to customary units:
- converts fields of Celsius to Fahrenheit (`_C to _F`)
- converts fields of km/h to Miles/h (`_kph to _mph`, `_km_h to _mi_h`)
- converts fields of mm to Inch (`_mm to _inch`)
- converts fields of mm/h to Inch/h (`_mm_h to _in_h`)
- converts fields of hPa to InchHg (`_hPa to _inHg`)
- converts fields of kPa to PSI (`_kPa to _PSI`)

## Filter output with bridges

You can grab the decoded output from rtl_433 in various ways, then process and relay it somewhere.

### Pipes

The simplest (but not very flexible or stable) way is to use pipes.
E.g. capture the decode JSON messages and relay the to MQTT with

`rtl_433 -F json -M utc | mosquitto_pub -t home/rtl_433 -l`

See also
[rtl_433_collectd_pipe.py](https://github.com/merbanan/rtl_433/blob/master/examples/rtl_433_collectd_pipe.py), and
[rtl_433_statsd_pipe.py](https://github.com/merbanan/rtl_433/blob/master/examples/rtl_433_statsd_pipe.py)
for other examples of this method.

### UDP

A better way is to use the Syslog-compatible UDP output to capture and relay the JSON message.

See also
[rtl_433_graphite_relay.py](https://github.com/merbanan/rtl_433/blob/master/examples/rtl_433_graphite_relay.py),
[rtl_433_mqtt_relay.py](https://github.com/merbanan/rtl_433/blob/master/examples/rtl_433_mqtt_relay.py), and
[rtl_433_statsd_relay.py](https://github.com/merbanan/rtl_433/blob/master/examples/rtl_433_statsd_relay.py)
for examples of this method.

### MQTT

If you already use the MQTT output, then you can capture the MQTT data, process it and inject derived data back.

See e.g. [rtl_433_mqtt_hass.py](https://github.com/merbanan/rtl_433/blob/master/examples/rtl_433_mqtt_hass.py)
for an example of this method.
