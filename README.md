rtl_433
=======

rtl_433 turns your Realtek RTL2832 based DVB dongle into a 433.92MHz generic data receiver

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
	[-p <ppm_error>] (default: 0)
	[-s <sample rate>] Set sample rate (default: 250000 Hz)
	[-S] Force sync output (default: async)
	= Demodulator options =
	[-R <device>] Listen only for the specified remote device (can be used multiple times)
	[-l <level>] Change detection level used to determine pulses [0-32767] (default: 8000)
	[-z <value>] Override short value in data decoder
	[-x <value>] Override long value in data decoder
	= Analyze/Debug options =
	[-a] Analyze mode. Print a textual description of the signal. Disables decoding
	[-A] Pulse Analyzer. Enable pulse analyzis and decode attempt
	[-D] Print debug info on event (repeat for more info)
	= File I/O options =
	[-t] Test signal auto save. Use it together with analyze mode (-a -t). Creates one file per signal
		 Note: Saves raw I/Q samples (uint8, 2 channel). Preferred mode for generating test files
	[-r <filename>] Read data from input file instead of a receiver
	[-m <mode>] Data file mode for input / output file (default: 0)
		 0 = Raw I/Q samples (uint8, 2 channel)
		 1 = AM demodulated samples (int16)
		 2 = FM demodulated samples (int16) (experimental)
		 Note: If output file is specified, input will always be I/Q
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
	[31] TFA-Twin-Plus-30.3049
	[32] Digitech XC0348 Weather Station
	[33] WT450
	[34] LaCrosse WS-2310 Weather Station
	[35] Esperanza EWS
	[36] Efergy e2 classic
	[37] Inovalley kw9015b rain and Temperature weather station
	[38] Generic temperature sensor 1
	[39] Acurite 592TXR Temperature/Humidity Sensor and 5n1 Weather Station
```


Examples:

| Command | Description
|---------|------------
| `rtl_433` | Will run the software in receive mode. Some sensor data can be received and decoded.
| `rtl_433 -a` | Will run in analyze mode and you will get a text description of the received signal.
| `rtl_433 -a -t` | Will run in analyze mode and save a test file per detected signal (gfile###.data). Format is uint8, 2 channels.
| `rtl_433 -r file_name` | Will run with a saved test file as input data.
| `rtl_433 file_name` | Will save the received signal data stream in a file (file may become large).

This software is mostly useable for developers right now.

Test Data
------------

Test data files for supported devices can be found here: https://github.com/merbanan/rtl_433_tests
Please add test data for all devices added to the code to facilitate regression testing.

Google Group
------------

https://groups.google.com/forum/#!forum/rtl_433
