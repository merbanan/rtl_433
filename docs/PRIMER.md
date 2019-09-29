# Primer on SDR concepts

A brief introduction to some concepts, most questions really only need a keyword to look it up.

> What is a signal?

In the simplest way think of it as a sound file. Load a sample in [Audacity](https://www.audacityteam.org/) and play it! But that's only the [sampled](https://en.wikipedia.org/wiki/Sampling_(signal_processing)), [quantized](https://en.wikipedia.org/wiki/Quantization_(signal_processing)) [baseband](https://en.wikipedia.org/wiki/Baseband) of the [radio wave](https://en.wikipedia.org/wiki/Radio_wave)...

> 433 MHz is 433920000 Hz? and so listening to 433920001 Hz is a different frequency that would not catch any 433 MHz signals?

"433" really means the 433.92 MHz [ISM-band](https://en.wikipedia.org/wiki/ISM_band). "434" would be more appropriate. It's a band of "sample rate"-width. E.g. 433.795 to 434.045 MHz at the default 433.92 "center frequency" and 250 kHz sample rate.

> How can you decrypt a signal? I am getting some signals with my SDR and it would be nice to know a bit more about them?

Mostly it's not encrypted but only coded and [modulated](https://en.wikipedia.org/wiki/Modulation).

> Is the data transmitted in binary code? Always?

There are multiple layers. Look up [ASK](https://en.wikipedia.org/wiki/Amplitude-shift_keying), 2-ASK / [OOK](https://en.wikipedia.org/wiki/On-off_keying), [FSK](https://en.wikipedia.org/wiki/Frequency-shift_keying), 2-FSK then [PCM](https://en.wikipedia.org/wiki/Pulse-code_modulation), [PWM](https://en.wikipedia.org/wiki/Pulse-width_modulation), [PPM](https://en.wikipedia.org/wiki/Pulse-position_modulation), [MANCHESTER](https://en.wikipedia.org/wiki/Manchester_code), [DMC](https://en.wikipedia.org/wiki/Differential_Manchester_encoding).

> Can I know the strength of the signal to know if the transmitter is close or far?

Only if there is no [AGC](https://en.wikipedia.org/wiki/Automatic_gain_control) or the current AGC value is reported (not with rtl-sdr).

> What is the meaning of the information I capture with rtl_433? It would be nice to know the meaning of each line or the different parts it shows?

It's the decoded transmission and some meta-data about the signal.

> Why was I getting all zeros when I was listening to my device? How could I change this to get the correct information?

Wrong assumption about decoding parameters. The suggested flex decoder is just a simple statistical heuristic.

> What is the information provided by the -A switch? pulse width, gap width, pulse period.....

See PWM.

> Can I buy and install a bigger antenna to get better signal? Or is there some limitation on the signal that will not allow this?

More metal is in fact the only reliable way to get better reception. But there are limits, s. wave length. And you could add directionality.

> Can I transmit a signal to my receiver thermometer and make it think that I am the transmission device? Does this mean that I can impersonate any transmitter device?

Sure. This almost always possible.

> Does a sensor need some kind of computer/arduino to send signals or can it work with just the sensor and some transmitter hardware?

There are specialized chips like the [EV1527](https://www.sunrom.com/get/206000) that do this. A general-purpose [MCU](https://en.wikipedia.org/wiki/Microcontroller) isn't likely to be used.

> And I would like to know a bit about other frequencies outside 433 MHz.

868 is also interesting. Mostly FSK is used, where 433 mostly uses OOK (ASK).

> I would like to send a doorbell button push to MQTT. rtl_433 doesn't decode it, but it's a simple button, so I only need a trigger event. With rtl_433 -a -R 0 I am able to catch the button with {25}... code. What is the correct way to send the raw data to MQTT?

Use `-A` and note the `-X` line. Then use that to write a flex decoder. See e.g. [EV1527-PIR-Sgooway.conf](https://github.com/merbanan/rtl_433/blob/master/conf/EV1527-PIR-Sgooway.conf).

Have fun.
