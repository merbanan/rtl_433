# Hardware tested with rtl_433

rtl_433 is known to work with or tested with the following SDR hardware:

## RTL-SDR

Actively tested and supported are Realtek RTL2832 based DVB dongles (and other similar devices supported by RTL-SDR).

See also [RTL-SDR](https://github.com/osmocom/rtl-sdr/).

## SoapySDR

Actively tested and supported are
- [LimeSDR USB](https://www.crowdsupply.com/lime-micro/limesdr)
- [LimeSDR mini](https://www.crowdsupply.com/lime-micro/limesdr-mini)
- [LimeNet Micro](https://www.crowdsupply.com/lime-micro/limenet-micro)
- [PlutoSDR](https://www.analog.com/en/design-center/evaluation-hardware-and-software/evaluation-boards-kits/adalm-pluto.html)
- [SDRplay](https://www.sdrplay.com/) (RSP1A tested)
- [HackRF One](https://greatscottgadgets.com/hackrf/) (reported, we don't have a receiver)
- [SoapyRemote](https://github.com/pothosware/SoapyRemote/wiki)

LimeSDR and LimeNet engineering samples were kindly provided by [MyriadRf](https://myriadrf.org/).

See also [SoapySDR](https://github.com/pothosware/SoapySDR/).

## Not supported

- Ultra cheap 1-bit (OOK) receivers, and antenna-on-a-raspi-pin
- CC1101, and alike special purpose / non general SDR chips
