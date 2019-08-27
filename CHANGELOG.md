# Changelog

## [Unreleased]

- HTTP server, JSON-RPC
- GPS meta data

## Release 19.08 (2019-08-29)

### Highlights

- Added MQTT output (#1016)
- Added stats reporting (#733)
- Added SoapySDR general keyword settings option, e.g. antenna
- Added new model keys option
- Changed Normalize odd general keys on devices (#1010)
- Changed Use battery_ok instead of battery for newmodel
- Added report model description option (#987)
- Added pulse data text file support (#967)
- Added color to console help output
- Fixed CF32 loader; addeded CS8, CF32 dumper

### Changed

- Added CurrentCost EnviR support (#1115)
- Added ESIC-EMT7170 power meter (#1132)
- Added LaCrosse-TX141Bv3 support (#1134)
- Added channel to inFactory-TH (#1133)
- Added man page rtl_433.1 (#1121)
- Added color to console help output
- Added support for Philips AJ7010 (#1047)
- Added frequency hopping signal support for win32 (#1128)
- Added Holman WS5029 decoder
- Added Acurite Rain 899 support
- Added support for Oregon scientific THGR328N and RTGR328N (#1107) (#1109)
- Added frequency hop on USR1 signal
- Added '-E hop' option
- Added option for multiple hop times
- Added sensor similar to GT-WT-02 (#1080)
- Added Rubicson 48659 Cooking Thermometer
- Added TFA Dostmann 30.3196 decoder (#983)
- Added support for HCS200 KeeLoq encoder (#1081)
- Added channel output to lacrosse_TX141TH_Bv2 (#1097)
- Added IKEA Sparsnäs decoder.
- Added support for Eurochron weather station sensor (#1090)
- Added MQTT topic format strings (#1079)
- Added two EV1527 based sample configurations (#1087)
- Added DirecTV RC66RX Remote Control
- Added support for Ecowitt temperature sensor
- Added Companion WTR001 decoder (#1055)
- Changed Thermopro TP12 also supports TP20 (#1061)
- Added configuration for PIR-EF4 sensor (#1049)
- Added Alecto WS-1200 v1/v2/DCF decoders to Fineoffset (#975)
- Added TS-FT002 decoder (#1015)
- Added Fine Offset WH32B support (#1040)
- Added LaCrosse-WS3600 support, change LaCrosse-WS to LaCrosse-WS2310
- Added LaCrosse WS7000 support (#1029) (#1030)
- Changed Omit humidity on Prologue if invalid
- Added MQTT output (#1016)
- Added stats reporting (#733)
- Added Interface Specification for data output (#827)
- Added checksum to Ambient Weather TX-8300
- Added Interlogix glassbreak subtype
- Added Jansite TPMS support (#1020)
- Added Oregon Scientific RTHN129 support (#941)
- Changed Use battery_ok instead of battery for newmodel
- Changed Update battery_low, temperatureN keys
- Changed Normalize odd general keys on devices (#1010)
- Fixed Efergy-e2 current reading exponent
- Added FS20 remote decoder (#999)
- Added SoapySDR general keyword settings option
- Added option to select antenna on SoapySDR devices (#968)
- Fixed CF32 loader; added CS8, CF32 dumper
- Added ASAN to Debug builds
- Changed tfa_twin_plus_30.3049: Add mic to output
- Added new model keys option
- Enhanced Kedsum, S3318, Esperanza with MIC (#985)
- Added support for XT300/XH300 soil moisture sensor (#946)
- Changed Schrader unit from bar to kPa
- Added report model description option (#987)
- Added native scale for SDRplay
- Added Chungear BCF-0019x2 example conf
- Changed GT-WT-02 to support newer timings; changed model name
- Added pulse data text file support (#967)
- Added Digitech XC0346 support to Fine Offset WH1050 (#922)
- Added bitbuffer NRZI(NRZS/NRZM) decodes
- Added Rosenborg/WH5 quirk to Fineoffset
- Added support for Silverline doorbells (#942)
- Changed TPMS Toyota to match shorter preamble
- Changed TPMS Citroen data readings
- Changed TPMS Renault data readings
- Added Digitech XC-0324 temperature sensor decoder (#849)
- Added sample rate switching
- Added Mongoose

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
