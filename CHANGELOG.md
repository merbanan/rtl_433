# Changelog

## [Unreleased]

- HTTP server, JSON-RPC
- GPS meta data

## Release 20.02 (2020-02-17)

### Highlights

- Added InfluxDB output (#1192)
- Added native Sigrok writer (#1297)
- Changed to newmodel keys default
- Fixed SoapySDR for 0.8 API
- Added new minmax FSK pulse detector
- Changed default to use new minmax detector and sample rate of 1MS/s for frequency above 800MHz
- Changed -a and -G option to discourage usage
- Improvements and support for many more sensors

### Changed

- Changed CurrentCost and EfergyOptical keys to have units (#1313)
- Added command line information when new defaults are active
- Added mic to csv output in the ert decoder
- Added subtype to DSC (#1318)
- Added meta to OOK output
- Fixed json escaping (closes #1299)
- Added ERT SCM protocol decoder
- Added return codes for most devices
- Changed remaining wind dir keys (see #1019)
- Fixed optparse strtod with rounding (#1308)
- Fixed for wmbus records parser
- Added integrity check for Thermopro TP11 and TP12
- Fixed conf eol comments (closes #1307)
- Added config for unknown car key.
- Fixed sync word for Honeywell CMI alarm systems
- Fixed Wno-format-security for nixos gcc9 (#1306)
- Fixed negative length in data_array (#1305)
- Added native Sigrok writer (#1297)
- Added checksum check for Rubicson 48659 meat thermometer
- Change Updated Fan 11t conf (#1287)
- Fixed failure on low sample rates (closes #1290)
- Improved format conversions
- Fixed radiohead-ask buffer overflow (closes #1289)
- Changed Enable IKEA Sparsnäs by default
- Changed cmake build to static lib
- Changed to newmodel keys default
- Changed model TFA-Drop-30.3233.01 to TFA-Drop
- Added config for Fan-11T fan remote (#1284)
- Added preliminary EcoWitt WS68 Anemometer support (#1283)
- Added EcoWitt WH40 support (closes #1275)
- Improved PCM RZ bit width detection
- Fixed for #1114 DSC Security Contact WS4945 (#1188)
- Fixed LaCrosse TX145wsdth repeat requirement
- Added preliminary LaCrosse TX141TH-BV3 support
- Fixed SoapySDR for 0.8 API
- Fixed Auriol AFW2A1 missing check
- Changed flex decode to count as successful output
- Added Nexus compatible sensor descriptions
- Improved LaCrosse TX29-IT support (#1279)
- Added LaCrosse TX145wsdth support (closes #1272)
- Changed KNX-RF output
- Added support for Lansen wmbus door/window sensor
- Improved PCM bit period detection
- Fixed OS PCR800 and RGR968 displayed unit name
- Fixed battery_level in Fineoffset-WH51
- Fixed type of battery_mv in Fineoffset-WH51 (#1274)
- Added reflected LFSR util
- Added support for TFA Drop 30.3233.01 (#1255)
- Added Auriol AFW2A1 support (#1230)
- Added Verisure Alarm config file
- Added wmbus mode S support and KNX RF telegram support.
- Added support for decoding Lansen and Bmeters wmbus based temperature/hygrometers
- Improved Honeywell 2Gig support
- Changed -a and -G option to discourage usage
- Added support for WS2032 weather station (#1208)
- Added timezone offset print option (#1207)
- Added LaCrosse TX141W support
- Added battery level to Fineoffset WH51
- Added Archos-TBH support (#1199)
- Added Oregon ID_THGR810a ID_WGR800a version ids (#1258)
- Improved OWL CM180 support (#1247)
- Added Holman iWeather WS5029 older PWM (closes #947)
- Added support for FineOffset/ECOWITT WH51 (#1242)
- Added config for 21 key remote
- Added rtlsdr_find_tuner_gain for exact gains
- Improved fineoffset more heuristics to separate WH65B and WH24
- Fixed missing csv fields on default disabled
- Improved Efergy Optical decoder (#1229)
- Added TX-button to some decoders (closes #1205)
- Improved for TFA pool temperature sensor (#1219)
- Added pulse analyzer support for read OOK data (#1216)
- Fixed ook input support bug from a9de888 (#1215)
- Fixed missing hop_time when reading file (closes #1211)
- Fixed Acurite 899 rain_mm conversion value (#1203)
- Fixed build files (closes #1201)
- Added more input format validation
- Changed FSK pulse detector mode option
- Fixed overlong msg in Radiohead (closes #1190)
- Added optional CSA checker to tests
- Added InfluxDB output (#1192)
- Fixed Hondaremote for missing first bit
- Fixed integer promotion for uint32_t fields (#1193)
- Fixed data format in ELV (#1187)
- Fixed closing brace bug in test bitbuffer (#1186)
- Fixed issue where bt_rain  decoder uses -1 as index
- Changed if the set frequency is > 800MHz then set sample rate to 1MS/s
- Added new minmax FSK pulse detector
- Added InfluxDB relay example script
- Fixed radiohead buffer underflow (#1181)
- Fixed range clamping on RSSI (#1179)
- Fixed unaligned sample buffer length (#1177)
- Fixed bad event return value in elantra (#1176)
- Fixed accounting if decoder misbehaves (#1175)
- Fixed ge_coloreffects undef behav (#1173)
- Added protocol selection to test bitbuffer input
- Added streaming test bitbuffers (#1062)
- Fixed parsing oversized bitbuffer (#1171)
- Added length check for interlogix device and update return codes (#1169)
- Added preamble check for microchip hcs200 to reduce false positives (#1170)
- Added unboxed types for data
- Added warnings on alloc failure
- Added alloc checks, fixes #1156
- Improved Compact doubles in mqtt devices topics
- Added Auriol-HG02832 support (#1166)
- Improved Honeywell for 2Gig, RE208 (#747)
- Changed Upgrade Mongoose 6.13+patches to 6.16+patches
- Fixed expand Efergy-e2CT exp range (#1163)
- Added RTL-SDR error code output
- Added Hyundai Elantra 2012 TPMS support (#1158)
- Added Norgo NGE101 support (#1042)
- Improved Convert read OOK pulse_data to current sample rate (#1160)
- Fixed Acurite 899 rain_mm format (#1154)
- Added gt_tmbbq05 parity check
- Changed GT-WT-03 added checksum (#1149)
- Change Updated QUIGG GT-TMBBQ-05 with MIC
- Changed GT-TMBBQ-05 added ID, finetuned pulse lengths (#1152)
- Added Sonoff RM433 conf example (#1150)
- Added support for Globaltronics GT-WT-03 (#1149)
- Added support for QUIGG GT-TMBBQ-05 (#1151)
- Improved Reorder some keys, normalize some keys (#998)
- Improved Oregon Scientific V3 preamble match
- Added support for Oregon Scientific WGR800X (#1045)
- Added Integration docs
- Added RRD example script

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
