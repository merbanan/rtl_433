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

    Usage: [-d device index (default: 0)]
           [-g gain (default: 0 for auto)]
           [-a analyze mode, print a textual description of the signal]
           [-t signal auto save, use it together with analyze mode (-a -t)
           [-l change the detection level used to determine pulses (0-3200) default: 10000]
           [-f [-f...] receive frequency[s], default: 433920000 Hz]
           [-s sample rate (default: 250000 Hz)]
           [-S force sync output (default: async)]
           [-r read data from file instead of from a receiver]
           [-p ppm_error (default: 0)]
           [-r test file name (indata)]
           [-m test file mode (0 rtl_sdr data, 1 rtl_433 data)]
           [-D print debug info on event]
           [-z override short value]
           [-x override long value]
           [-R listen only for the specified remote device (can be used multiple times)]
           filename (a '-' dumps samples to stdout)
           
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
           [16] AlectoV1 Weather Sensor
           [17] Cardin S466-TX2
           [18] Fine Offset Electronics, WH-2 Sensor
           [19] Nexus Temperature & Humidity Sensor
           [20] Ambient Weather Temperature Sensor
           [21] Calibeur RF-104 Sensor
           [22] X10 RF
           [23] DSC (Digital Security Controls)


Examples:

| Command | Description
|---------|------------
| `rtl_433 -a` | will run in analyze mode and you will get a text log of the received signal.
| `rtl_433 -a file_name` | will save the demodulated signal in a file. The format of the file is 48kHz 16 bit samples.
| `rtl_433` | will run the software in receive mode. Some sensor data can be receviced.

This software is mostly useable for developers right now.

Google Group
------------

https://groups.google.com/forum/#!forum/rtl_433

