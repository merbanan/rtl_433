# Changelog

## [Unreleased]

- MQTT output
- HTTP server, JSON-RPC
- GPS meta data
- Stats reporting
- SoapySDR: added option to select antenna [-t]

## Release 18.12 (2018-12-16)

### Highlights

- Added conf file support with examples in etc/rtl_433/
- Default KV output has pretty colors on console
- Added meta data for levels, precision time, protocol, debug, tagging
- Added rtl_tcp input (#894)
- Added SoapySDR support with CU8/CS16/F32 input/output conversions (#842)
- Added VCD output, Sigrok pulseview converter

### Changed

- Install example conf files will to etc/rtl_433/
- Default output is terse with just the most important info
- Deprecate option q and D for new v to set verbosity
- Default KV output has pretty colors on console
- Added debug bits output option
- Added protocol number meta data option
- Added precision time and time report options (#905)
- Deprecate option t and I for new S none|all|unknown|known
- Changed to use pulse detect to track and grab frames
- Added rtl_tcp input (#894)
- Added bitrow debugging output helper
- Added bitbuffer_debug, bitrow_print, bitrow_debug
- Changed flex to use keys for all values (#885)
- Allow multiple input files, positional args are input files
- Added option for output tagging
- Added conf examples for generic SCV2260 and PT2260
- Added a conf file parser (#790)
- Added negative protocol numbers to disable a device
- Added Freq/RSSI/SNR output to data_acquired_handler (#865)
- Added flex suggestion to analyzer output, switch to unit of us
- Added null output option (suppress default KV)
- Added option to skip the tests to be built. (#832)
- Added SoapySDR support (#842)
- Added CU8/CS16 output conversion
- Improved dumpers to allow multiple dumpers
- Removed rtlsdr sync mode
- Added VCD output
- Added Sigrok pulseview converter
- Added f32 output modes
- Added flex getter (#786)
- Added version (-V) and help (-h) option (#810)
- Added example MS Visual Studio 2015 project (#789)
- Added CS16 input and output (#773)
- Added preamble option to flex decoder

### Added and improved devices

- Added Gust to Hideki, report proper mph (#891)
- Changed raincounter_raw field to rain_inch for acurite (#893)
- Removed EC3k, converted to flex conf
- Removed Valeo, converted to flex conf
- Removed Steffen, converted to flex conf
- Changed ELV-EM1000, ELV-WS2000 to structured output
- Changed X10-RF to structured output
- Changed Lightwave-RF to structured output
- Added confs ported from old devices
- Improved Fine Offset WH-3080 to support new version Watts/m value calculation
- Added support for Bresser Weather Center 5-in-1
- Added Biltema rain gauge protocol decoder, disabled by default
- Added ESA 1000/2000 protocol decoder
- Added support for Honeywell Wireless Doorbell
- Improved inFactory with added checks, enabled by default
- Added Maverick et73
- Added Ambient Weather WH31E (#882)
- Added AmbientWeather-TX8300 (TFA 30.3211.02) support
- Added Emos TTX201 (#782)
- Added Hideki / Cresta temperature sensor (#858)
- Added Fine Offset WH65B support (#845)
- Added AcuRite 3-n-1 (#720)
- Added PMV-107J TPMS (#825)
- Added Fine Offset WH65b support
- Added Fine Offset WH24 (#809)
- Added TP08 remote thermometer (#750)
- Added WT0124 Pool Thermometer
- Added Hyundai WS sensor (#779)
- Added M-Bus (EN 13757-4) - Data Link layer (#768)
- Improved RadioHead to unify the applications
- Added Sensible Living protocol (#742)
- Added Oregon Scientific UVR128 UV sensor (#738)
- Added Pacific PMV-C210 TMPS support (#717)
- Added SimpliSafe Sensor (#721)

## Release 18.05 (2018-05-02)

### Highlights

- Preparations for features like MQTT and SoapySDR
- Syslog for simple network output
- Rewritten demodulators to support a "precise" mode using a given tolerance and optional sync symbols
- Simplified data output layers

### Changed

- Added conversion hPA/inHG, kPa/PSI (#711)
- Added remote syslog output
- Added a flexible general purpose decoder (#647)
- Added git version info to usage output if available
- Added number suffixes on e.g. frequency, samplerate, duration, hoptime
- Added Profile build type using GPerfTools
- Changed grab file name to gNNN_FFFM_RRRk.cu8 (#642)
- Added option to use receiver serial number -d :SERIAL (#648)
- Added option to stop after outputting successful event(s)
- Changed to new data API
- Added option to verify simulated decoding of raw data

### Added and improved devices

- Added decoder for Dish Network UHF Remote 6.3 (#700)
- Added interlogix devices driver (#649)
- Added Euroster 3000TX, Elro DB270 (#683)
- Added x10_sec device for decoding X10 Security RF signals (#671)
- Added device LaCrosse TX141 support to lacrosse_TX141TH_Bv2.c (#670)
- Added GE Color Effects Remote indent and MAX_PROTOCOLS
- Added support for Telldus variants of fineoffset (#639)
- Added support to Oregon Scientific RTGN129
- Added device NEXA LMST-606 magnetic sensor
- Added support for Philips outdoor temperature sensor
- Added support for Ford car remote.
- Added support for the Thermopro TP-12.
- Added infactory sensor
- Added Renault TPMS sensor
- Added Ford TPMS sensor
- Added Toyota TPMS sensor
- Added GE Color Effects remote control
- Added Generic off-brand wireless motion sensor and alarm system
- Added Wireless Smoke and Heat Detector GS 558
- Added Solight TE44 wireless thermometer
