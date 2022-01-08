# Introduction to I/Q formats

SDR data is exchanged and saved in different formats.
There are formats for raw I/Q sample data and formats with demodulated pulse data.

## I/Q sample data formats

I/Q stands for "In-phase / Quadrature", the raw data format used by SDR receivers and transmitters.
A sample consists of an I and Q value, each commonly of 8, 12, or 16-bit. This is called "interleaved" in audio or video data.

The data can be processed similar to a two-channel audio signal, although at a much higher sample rate.

::: tip
Common sample rates with RTL-SDR receivers are 250 kHz and 1024 kHz, also 1 MHz (1000 kHz).
:::

The nature of an I/Q sample allows to use a bandwidth equal to the sample rate
(with a purely real signal the Nyquist–Shannon sampling theorem would only allow half the bandwidth).

Generally the data formats are header- (and thus metadata-)less,
the used center frequency and sample rate must be transferred separately or encoded in the filename.

Formats differ in sample-(bit-)width and (bit-)number format,
used bit-widths are 4, 8, 12, 16, 32, and 64, bit-formats are unsigned integer, signed integer, and float:

- `.cu4`: Complex (I/Q), Unsigned integer, 4-bit per value (8 bit per sample)
- `.cs4`: Signed integer
- `.cu8` (`.data` `.complex16u`): 8-bit per value (16 bit per sample)
- `.cs8` (`.complex16s`)
- `.cu12`: 12-bit per value (24 bit per sample)
- `.cs12`
- `.cu16`: 16-bit per value (32 bit per sample)
- `.cs16`
- `.cu32`: 32-bit per value (64 bit per sample)
- `.cs32`
- `.cu64`: 64-bit per value (128 bit per sample)
- `.cs64`
- `.cf32` (`.cfile` `.complex`): Float, 32-bit per value (64 bit per sample)
- `.cf64`: Double Float, 64-bit per value (128 bit per sample)

Also used but rarely supported are audio files containing I/Q data:
- `.wav`
- `.bwf`:  Broadcast Wave Format

The "native" format for RTL-SDR receivers is `.cu8`, for other receivers likely `.cs16`.
Most receivers only sample with 12-bit per channel, using `.cs12` will be more compact although not as widely supported.

The rtl_433 program supports most of these formats and allows to read, write, or convert them, e.g.:

- `rtl_433 -w FILE.cu8`: write received data to sample file
- `rtl_433 -w FILE.cu8 FILE.cs16`: convert sample file

## Pulse data formats

Demodulated data can be stored in a readable text-format with file extension `.ook`, also `.fsk` or `.psk`.
A header contains meta data about the demodulation (2-ASK / ook, 2-FSK / fsk, ...) and the extension is informational only.
Each regular line in the file format contains a number for pulse duration and a number for gap duration.
For FSK or PSK demodulation these are mark and space duration.

There is also the `.vcd` format which can carry the same information and might be useful with traditional signal data software.
It can optionally also encode more than two states, e.g. (4-FSK), this isn't used however.

A very compact format is `rfraw:`, usually just one line of code.
This format encodes quantized pulse/gap durations with a maximum of eight different durations.

There are also formats for demodulated but "raw" amplitude or frequency,
e.g. `.am.s16`, `.fm.s16` similar to the above formats but with only one "channel".

The SigRok `.sr` format is a Zip and combines multiple files for easy viewing with SigRok Pulseview.

::: tip
Install SigRok Pulseview and write a SigRok file. The overwrite option (uppercase `-W`) will automatically open Pulseview.
:::

The rtl_433 program can create all these formats from live data or sample files, e.g.:

- `rtl_433 -w FILE.ook`: write received data to ook file
- `rtl_433 -w FILE.ook FILE.cu8`: convert sample file to ook file

## File name meta data

In addition to the file extension meta data about the center frequency and sample rate are encoded in the filename.

- `433.92M` : A decimal number suffixed with `M` denotes the center frequency
- `1000k` : A decimal number suffixed with `k` denotes the sample rate

Each part of the filename must be separated by an underscore.
Even with low frequencies or high sample rates the suffix is fixed,

::: warning
`433920k` is not a valid frequency specification and `1M` is not a valid sample rate specification in filenames.
:::

## File viewers

All raw I/Q sample data formats and most demodulated pulse data formats can be visualized with
the [triq I/Q Spectrogram and Pulsedata viewer](https://triq.org/pdv/).
