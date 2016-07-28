rtl_433
=======

rtl_433 turns your Realtek RTL2832 based DVB dongle into a 433.92MHz generic data receiver

How to add support for unsupported sensors
------------------------------------------

Read the Test Data section at the bottom.


Installation instructions:
--------------------------

Compiling rtl_433 requires [rtl-sdr](http://sdr.osmocom.org/trac/wiki/rtl-sdr) to be installed.

Installation using cmake:

    cd rtl_433/
    mkdir build
    cd build
    cmake ../
    make
    make install

Installation using autoconf:

    cd rtl_433/
    autoreconf --install
    ./configure
    make
    make install


Running:
--------

    rtl_433 -h

```
Usage:	= Tuner options =
	[-d <device index>] (default: 0)
	[-g <gain>] (default: 0 for auto)
	[-f <frequency>] [-f...] Receive frequency(s) (default: 433920000 Hz)
	[-p <ppm_error] Correct rtl-sdr tuner frequency offset error (default: 0)
	[-s <sample rate>] Set sample rate (default: 250000 Hz)
	[-S] Force sync output (default: async)
	= Demodulator options =
	[-R <device>] Listen only for the specified remote device (can be used multiple times)
	[-l <level>] Change detection level used to determine pulses [0-32767] (0 = auto) (default: 8000)
	[-z <value>] Override short value in data decoder
	[-x <value>] Override long value in data decoder
	= Analyze/Debug options =
	[-a] Analyze mode. Print a textual description of the signal. Disables decoding
	[-A] Pulse Analyzer. Enable pulse analyzis and decode attempt
	[-D] Print debug info on event (repeat for more info)
	[-q] Quiet mode, suppress non-data messages
	[-W] Overwrite mode, disable checks to prevent files from being overwritten
	= File I/O options =
	[-t] Test signal auto save. Use it together with analyze mode (-a -t). Creates one file per signal
		 Note: Saves raw I/Q samples (uint8 pcm, 2 channel). Preferred mode for generating test files
	[-r <filename>] Read data from input file instead of a receiver
	[-m <mode>] Data file mode for input / output file (default: 0)
		 0 = Raw I/Q samples (uint8, 2 channel)
		 1 = AM demodulated samples (int16 pcm, 1 channel)
		 2 = FM demodulated samples (int16) (experimental)
		 3 = Raw I/Q samples (cf32, 2 channel)
		 Note: If output file is specified, input will always be I/Q
	[-F] kv|json|csv Produce decoded output in given format. Not yet supported by all drivers.
	[<filename>] Save data stream to output file (a '-' dumps samples to stdout)

Supported devices:
	[01] Silvercrest Remote Control
	[02] Rubicson Temperature Sensor
	[03] Prologue Temperature Sensor
	[04] Waveman Switch Transmitter
	[05] Steffen Switch Transmitter
	[06] ELV EM 1000
	[07] ELV WS 2000
	[08] LaCrosse TX Temperature / Humidity Sensor
	[09] Acurite 5n1 Weather Station
	[10] Acurite 896 Rain Gauge
	[11] Acurite Temperature and Humidity Sensor
	[12] Oregon Scientific Weather Sensor
	[13] Mebus 433
	[14] Intertechno 433
	[15] KlikAanKlikUit Wireless Switch
	[16] AlectoV1 Weather Sensor (Alecto WS3500 WS4500 Ventus W155/W044 Oregon)
	[17] Cardin S466-TX2
	[18] Fine Offset Electronics, WH-2 Sensor
	[19] Nexus Temperature & Humidity Sensor
	[20] Ambient Weather Temperature Sensor
	[21] Calibeur RF-104 Sensor
	[22] X10 RF
	[23] DSC Security Contact
	[24] Brennstuhl RCS 2044
	[25] GT-WT-02 Sensor
	[26] Danfoss CFR Thermostat
	[27] Energy Count 3000 (868.3 MHz)
	[28] Valeo Car Key
	[29] Chuango Security Technology
	[30] Generic Remote SC226x EV1527
	[31] TFA-Twin-Plus-30.3049 and Ea2 BL999
	[32] Fine Offset WH1080 Weather Station
	[33] WT450
	[34] LaCrosse WS-2310 Weather Station
	[35] Esperanza EWS
	[36] Efergy e2 classic
	[37] Inovalley kw9015b rain and Temperature weather station
	[38] Generic temperature sensor 1
	[39] Acurite 592TXR Temperature/Humidity Sensor and 5n1 Weather Station
	[40] Acurite 986 Refrigerator / Freezer Thermometer
	[41] HIDEKI TS04 Temperature and Humidity Sensor
	[42] Watchman Sonic / Apollo Ultrasonic / Beckett Rocket oil tank monitor
	[43] CurrentCost Current Sensor
	[44] OpenEnergyMonitor emonTx v3
	[45] HT680 Remote control
        [46] S3318P Temperature & Humidity Sensor
        [47] Akhan 100F14 remote keyless entry
        [48] Quhwa
	[49] Oregon Scientific v1 Temperature Sensor
        [50] Proove
        [51] Bresser Thermo-/Hygro-Sensor 3CH
	[52] Springfield PreciseTemp Temperature and Soil Moisture
        [53] Oregon Scientific SL109H Temperature & Humidity Sensor
	[54] Acurite 606TX Temperature Sensor 
        [55] TFA pool temperature sensor
        [56] Kedsum Temperature & Humidity Sensor
        [57] blyss DC5-UK-WH (433.92 MHz)
        [58] Steelmate TPMS
        [59] Schraeder TPMS
        [60] LightwaveRF
        [61] Elro DB286A Doorbell
        [62] Efergy Optical
```


Examples:

| Command | Description
|---------|------------
| `rtl_433` | Default receive mode, attempt to decode all known devices
| `rtl_433 -p NN -R 1 -R 9 -R 36 -R 40` | Typical usage: Enable device decoders for desired devices. Correct rtl-sdr tuning error (ppm offset).
| `rtl_433 -a` | Will run in analyze mode and you will get a text description of the received signal.
| `rtl_433 -A` | Enable pulse analyzer. Summarizes the timings of pulses, gaps, and periods. Can be used in either the normal decode mode, or analyze mode.
| `rtl_433 -a -t` | Will run in analyze mode and save a test file per detected signal (gfile###.data). Format is uint8, 2 channels.
| `rtl_433 -r file_name` | Play back a saved data file. 
| `rtl_433 file_name` | Will save everything received from the rtl-sdr during the session into a single file. The saves file may become quite large depending on how long rtl_433 is left running. Note: saving signals into individual files wint `rtl_433 -a -t` is preferred.

This software is mostly useable for developers right now.


Supporting Additional Devices and Test Data
-------------------------------------------

The first step in decoding new devices is to record the signals using `-a -t`. The signals will be
stored individually in files named gfileNNN.data that can be played back with `rtl_433 -r gfileNNN.data`.

These files are vital for understanding the signal format as well as the message data.  Use both analyzers
`-a` and `-A` to look at the recorded signal and determine the pulse characteristics, e.g. `rtl_433 -r gfileNNN.data -a -A`.

Make sure you have recorded a proper set of test signals representing different conditions together
with any and all information about the values that the signal should represent. For example, make a
note of what temperature and/or humidity is the signal encoding. Ideally, capture a range of data
values, such a different temperatures, to make it easy to spot what part of the message is changing.

Add the data files, a text file describing the captured signals, pictures of the device and/or
a link the manufacturer's page (ideally with specifications) to the rtl_433_tests
github repository. Follow the existing structure as best as possible and send a pull request.

https://github.com/merbanan/rtl_433_tests

Please don't open a new github issue for device support or request decoding help from others
until you've added test signals and the description to the repository.

The rtl_433_test repository is also used to help test that changes to rtl_433 haven't caused any regressions.

Google Group
------------

Join the Google group, rtl_433, for more information about rtl_433:
https://groups.google.com/forum/#!forum/rtl_433
