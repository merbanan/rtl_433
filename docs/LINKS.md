# Links to tools and related projects

## SDR Inputs/Drivers

- [RTL-SDR](https://github.com/osmocom/rtl-sdr/)
- [SoapySDR](https://github.com/pothosware/SoapySDR/)

## Analysis

- [SigRok](https://sigrok.org/) [PulseView](https://sigrok.org/wiki/PulseView)
- [Audacity](https://www.audacityteam.org/)
- [iqSpectrogram](http://triq.net/iqs) to visualize sample files
- [BitBench](http://triq.net/bitbench) to analyze data formats

# Related projects

- [ShinySDR](https://shinysdr.switchb.org/)
  Web remote-controllable SDR receiver application supporting multiple simultaneous hardware devices and demodulators, including rtl_433 and other decoding tools.

- [HASS addon to convert rtl433 output to mqtt](https://github.com/james-fry/hassio-addons/blob/master/rtl4332mqtt/rtl2mqtt.sh)

- [rtl_fl2k_433](https://github.com/winterrace2/rtl_fl2k_433)
   an RX/TX prototyping tool. Aims to be a comfortable, GUI-based bridge between RTL-SDR dongles on RX side and cheap FL2K dongles on TX side. Currently, the GUI is available for Win64 only.

- [rtl_433 with Snap7](https://github.com/merbanan/rtl_433/issues/950)
  to inject weather data to industrial control system (PLC - Siemens S7-300 or compatible VIPA) coming from Weather station WH1080.

- [Domoticz](https://www.domoticz.com/)
   rtl_433 is usable from domoticz with a quite good integration: Domoticz launch rtl_433 with no data detection (relaunch rtl_433 if so) and process csv output format. All command line arguments are usable.

- [WeeWx](http://weewx.com/)
  the weewx-sdr driver gets data from rtl_433 and feeds it into weewx. from there the data can be combined with data from other sources, displayed using any of the many weewx skins, and/or uploaded to many different web services. the first weewx-sdr release was in 2016.
  S.a. https://github.com/matthewwall/weewx-sdr https://github.com/weewx/weewx/wiki#skins https://github.com/weewx/weewx/wiki#uploaders
