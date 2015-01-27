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

    Usage: [-d device_index (default: 0)]
           [-g gain (default: 0 for auto)]
           [-a analyze mode, print a textual description of the signal]
           [-t signal auto save, use it together with analyze mode (-a -t)
           [-l change the detection level used to determine pulses (0-3200) default: 10000]
           [-f [-f...] receive frequency[s], default: 433920000 Hz]
           [-s samplerate (default: 250000 Hz)]
           [-S force sync output (default: async)]
           [-r read data from file instead of from a receiver]
           [-p ppm_error (default: 0)]
           [-r test file name (indata)]
           [-m test file mode (0 rtl_sdr data, 1 rtl_433 data)]
           [-D print debug info on event
           [-z override short value
           [-x override long value
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

