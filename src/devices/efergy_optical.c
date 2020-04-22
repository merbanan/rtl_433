/** @file
    Efergy IR Optical energy consumption meter.

    Copyright (C) 2016 Adrian Stevenson <adrian_stevenson2002@yahoo.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Efergy IR is a devices that periodically reports current energy consumption
on frequency ~433.55 MHz. The data that is transmitted consists of 8
bytes:

- Byte 0-2: Start bits (0000), then static data (probably device id)
- Byte 3: seconds (64: 30s - red led; 80: 60s - orange led; 96: 90s - green led)
- Byte 4-7: all zeros
- Byte 8: Pulse Count
- Byte 9: sample frequency (15 seconds)
- Byte 10-11: bytes 0-9 crc16 xmodem XOR with FF

if pulse count <3 then energy =(( pulsecount/impulse-perkwh) * (3600/seconds))
else  energy= ((pulsecount/n_imp) * (3600/seconds))

Transmitter can operate in 3 modes (signaled in bytes[3]):
- red led: information is sent every 30s
- orange led: information is sent every 60s
- green led: information is sent every 90s

To get the mode: short-push the physical button on transmitter.
To set the mode: long-push the physical button on transmitter.
*/

#include "decoder.h"

static int efergy_optical_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    unsigned num_bits = bitbuffer->bits_per_row[0];
    uint8_t *bytes = bitbuffer->bb[0];
    float energy, n_imp;
    int pulsecount;
    float seconds;
    data_t *data;
    uint16_t crc;
    uint16_t csum1;

    if (num_bits < 96 || num_bits > 100)
        return DECODE_ABORT_LENGTH;

    // The bit buffer isn't always aligned to the transmitted data, so
    // search for data start and shift out the bits which aren't part
    // of the data. The data always starts with 0000 (or 1111 if
    // gaps/pulses are mixed up).
    while ((bytes[0] & 0xf0) != 0xf0 && (bytes[0] & 0xf0) != 0x00) {
        num_bits -= 1;
        if (num_bits < 96)
            return DECODE_ABORT_EARLY;

        for (unsigned i = 0; i < (num_bits + 7) / 8; ++i) {
            bytes[i] <<= 1;
            bytes[i] |= (bytes[i + 1] & 0x80) >> 7;
        }
    }

    // Sometimes pulses and gaps are mixed up. If this happens, invert
    // all bytes to get correct interpretation.
    if (bytes[0] & 0xf0) {
        for (unsigned i = 0; i < 12; ++i) {
            bytes[i] = ~bytes[i];
        }
    }

    if (decoder->verbose > 1) {
        bitbuffer_printf(bitbuffer, "%s: matched ", __func__);
    }

    // reject false positives
    if ((bytes[8] == 0) && (bytes[9] == 0) && (bytes[10] == 0) && (bytes[11] == 0)) {
        return DECODE_FAIL_SANITY;
    }

    // Calculate checksum for bytes[0..9]
    // crc16 xmodem with start value of 0x00 and polynomic of 0x1021 is same as CRC-CCITT (0x0000)
    // start of data, length of data=10, polynomic=0x1021, init=0x0000

    csum1 = (bytes[10] << 8) | (bytes[11]);

    crc = crc16(bytes, 10, 0x1021, 0x0000);

    if (crc != csum1) {
        if (decoder->verbose)
            fprintf(stderr, "%s: CRC error.\n", __func__);
        return DECODE_FAIL_MIC;
    }

    unsigned id = ((unsigned)bytes[0] << 16) | (bytes[1] << 8) | (bytes[2]);

    // this setting depends on your electricity meter's optical output
    n_imp = 3200;

    // interval:
    // - red led (every 30s):    bytes[3]=64 (0100 0000)
    // - orange led (every 60s): bytes[3]=80 (0101 0000)
    // - green led (every 90s):  bytes[3]=96 (0110 0000)
    seconds = (((bytes[3] & 0x30 ) >> 4 ) + 1) * 30.0;

    pulsecount = bytes[8];

    energy = (((float)pulsecount/n_imp) * (3600/seconds));

    //New code for calculating various energy values for differing pulse-kwh values
    const int imp_kwh[] = {4000, 3200, 2000, 1000, 500, 0};
    for (unsigned i = 0; imp_kwh[i] != 0; ++i) {
        energy = (((float)pulsecount/imp_kwh[i]) * (3600/seconds));

        /* clang-format off */
        data = data_make(
                "model",    "Model",        DATA_STRING, _X("Efergy-Optical","Efergy Optical"),
                "id",       "",             DATA_INT,   id,
                "pulses", "Pulse-rate",     DATA_INT, imp_kwh[i],
                "pulsecount", "Pulse-count", DATA_INT, pulsecount,
                _X("energy_kWh","energy"),   "Energy",       DATA_FORMAT, "%.03f kWh", DATA_DOUBLE, energy,
                "mic",       "Integrity",   DATA_STRING, "CRC",
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
    }
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "pulses",
        "pulsecount",
        "energy", // TODO: remove this
        "energy_kWh",
        NULL,
};

r_device efergy_optical = {
        .name        = "Efergy Optical",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 64,
        .long_width  = 136,
        .sync_width  = 500,
        .reset_limit = 400,
        .decode_fn   = &efergy_optical_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
