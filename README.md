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

    Usage:  [-d device_index (default: 0)]
        [-g gain (default: 0 for auto)]
        [-a analyze mode, print a textual description of the signal]
        [-l change the detection level used to determine pulses (0-32000) default 10000]
        [-f change the receive frequency, default is 433.92MHz]
        [-S force sync output (default: async)]
        [-r read data from file instead of from a receiver]
        filename (a '-' dumps samples to stdout)


Examples:

| Command | Description
|---------|------------
| `rtl_433 -a` | will run in analyze mode and you will get a text log of the received signal.
| `rtl_433 -a file_name` | will save the demodulated signal in a file. The format of the file is 48kHz 16 bit samples.
| `rtl_433` | will run the software in receive mode. Some sensor data can be receviced.

This software is mostly useable for developers right now.

Supported Devices
-----------------

    Rubicson Temperature Sensor
    Silvercrest Remote Control
    ELV EM 1000
    ELV WS 2000
    Waveman Switch Transmitter
    Steffen Switch Transmitter
    Acurite 5n1 Weather Station
    Acurite Temperature and Humidity Sensor
    Acurite 896 Rain Gauge
    LaCrosse TX Temperature / Humidity Sensor
    Oregon Scientific Weather Sensor
    KlikAanKlikUit Wireless Switch
    AlectoV1 Weather Sensor
    Intertechno 433
    Mebus 433

Google Group
------------

https://groups.google.com/forum/#!forum/rtl_433

