/* Efergy IR is a devices that periodically reports current energy consumption
 * on frequency ~433.55 MHz. The data that is transmitted consists of 8
 * bytes:
 *
 * Byte 1-4: Start bits (0000), then static data (probably device id)
 * Byte 5-7: all zeros
 * Byte 8: Pulse Count
 * Byte 9: sample frequency (15 seconds)
 * Byte 10: seconds
 * Byte 11: bytes0-10 crc16 xmodem XOR with FF
 * Byte 12: ?crc16 xmodem
 * if pulse count <3 then energy =(( pulsecount/impulse-perkwh) * (3600/seconds))
 * else  energy= ((pulsecount/n_imp) * (3600/seconds))
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static int efergy_optical_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    unsigned num_bits = bitbuffer->bits_per_row[0];
    uint8_t *bytes = bitbuffer->bb[0];
    double energy, n_imp;
    double pulsecount;
    double seconds;
    data_t *data;
    uint16_t crc;
    uint16_t csum1;

    if (num_bits < 96 || num_bits > 100)
        return 0;

    // The bit buffer isn't always aligned to the transmitted data, so
    // search for data start and shift out the bits which aren't part
    // of the data. The data always starts with 0000 (or 1111 if
    // gaps/pulses are mixed up).
    while ((bytes[0] & 0xf0) != 0xf0 && (bytes[0] & 0xf0) != 0x00) {
        num_bits -= 1;
        if (num_bits < 96)
            return 0;

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
        return 0;
    }

    // Calculate checksum for bytes[0..9]
    // crc16 xmodem with start value of 0x00 and polynomic of 0x1021 is same as CRC-CCITT (0x0000)
    // start of data, length of data=10, polynomic=0x1021, init=0x0000

    csum1 = ((bytes[10]<<8)|(bytes[11]));

    crc = crc16(bytes, 10, 0x1021, 0x0000);

    if (crc != csum1) {
        if (decoder->verbose)
            fprintf(stderr, "%s: CRC error.\n", __func__);
        return 0;
    }

    // this setting depends on your electricity meter's optical output
    n_imp = 3200;

    pulsecount = bytes[8];
    seconds = bytes[9];

    //some logic for low pulse count not sure how I reached this formula
    if (pulsecount < 3) {
        energy = ((pulsecount/n_imp) * (3600/seconds));
    }
    else {
        energy = ((pulsecount/n_imp) * (3600/30));
    }
    //New code for calculating various energy values for differing pulse-kwh values
    const int imp_kwh[] = {3200, 2000, 1000, 500, 0};
    for (unsigned i = 0; imp_kwh[i] != 0; ++i) {
        if (pulsecount < 3) {
            energy = ((pulsecount/imp_kwh[i]) * (3600/seconds));
        }
        else {
            energy = ((pulsecount/imp_kwh[i]) * (3600/30));
        }
        data = data_make(
                "model",    "Model",        DATA_STRING, _X("Efergy-Optical","Efergy Optical"),
                "pulses",   "Pulse-rate",   DATA_FORMAT, "%i", DATA_INT, imp_kwh[i],
                "energy",   "Energy",       DATA_FORMAT, "%.03f KWh", DATA_DOUBLE, energy,
                NULL);
        decoder_output_data(decoder, data);
    }
    return 1;
}

static char *output_fields[] = {
    "model",
    "pulses",
    "energy",
    NULL
};

r_device efergy_optical = {
    .name           = "Efergy Optical",
    .modulation     = FSK_PULSE_PWM,
    .short_width    = 64,
    .long_width     = 136,
    .sync_width     = 500,
    .reset_limit    = 400,
    .decode_fn      = &efergy_optical_callback,
    .disabled       = 0,
    .fields         = output_fields
};
