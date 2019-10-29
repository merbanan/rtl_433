/** @file
    Philips AJ3650 outdoor temperature sensor.

    Copyright (C) 2017 Chris Coffey <kpuc@sdf.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Philips outdoor temperature sensor -- used with various Philips clock
radios (tested on AJ3650).

Not tested, but these should also work: AJ260 ... maybe others?

A complete message is 112 bits:
- 4-bit initial preamble, always 0
- 4-bit packet separator, always 0, followed by 32-bit data packet.
- Packets are repeated 3 times for 108 bits total.

32-bit data packet format:

    0001cccc tttttttt tt000000 0b0?ssss

- c: channel: 0=channel 2, 2=channel 1, 4=channel 3 (4 bits)
- t: temperature in Celsius: subtract 500 and divide by 10 (10 bits)
- b: battery status: 0 = OK, 1 = LOW (1 bit)
- ?: unknown: always 1 in every packet I've seen (1 bit)
- s: CRC: non-standard CRC-4, poly 0x9, init 0x1

Pulse width:
- Short: 2000 us = 0
- Long: 6000 us = 1
Gap width:
- Short: 6000 us
- Long: 2000 us
Gap width between packets: 29000 us

Presumably the 4-bit preamble is meant to be a sync of some sort,
but it has the exact same pulse/gap width as a short pulse, and
gets processed as data.
*/

#include "decoder.h"

#define PHILIPS_BITLEN       112
#define PHILIPS_PACKETLEN    4
#define PHILIPS_STARTNIBBLE  0x0

static int philips_aj3650_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    /* Map channel values to their real-world counterparts */
    uint8_t const channel_map[] = {2, 0, 1, 0, 3};

    uint8_t *bb;
    unsigned int i;
    uint8_t a, b, c;
    uint8_t packet[PHILIPS_PACKETLEN];
    uint8_t c_crc;
    uint8_t channel, battery_status;
    int temp_raw;
    float temperature;
    data_t *data;

    /* Invert the data bits */
    bitbuffer_invert(bitbuffer);

    /* Correct number of rows? */
    if (bitbuffer->num_rows != 1) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: wrong number of rows (%d)\n",
                    __func__, bitbuffer->num_rows);
        }
        return 0;
    }

    /* Correct bit length? */
    if (bitbuffer->bits_per_row[0] != PHILIPS_BITLEN) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: wrong number of bits (%d)\n",
                    __func__, bitbuffer->bits_per_row[0]);
        }
        return 0;
    }

    bb = bitbuffer->bb[0];

    /* Correct start sequence? */
    if ((bb[0] >> 4) != PHILIPS_STARTNIBBLE) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: wrong start nibble\n", __func__);
        }
        return 0;
    }

    /* Compare and combine the 3 repeated packets, with majority wins */
    for (i = 0; i < PHILIPS_PACKETLEN; i++) {
        a = bb[i+1]; /* First packet - on byte boundary */
        b = (bb[i+5] << 4) | (bb[i+6] >> 4 & 0xf); /* Second packet - not on byte boundary */
        c = bb[i+10]; /* Third packet - on byte boundary */

        packet[i] = (a & b) | (b & c) | (a & c);
    }

    /* If debug enabled, print the combined majority-wins packet */
    if (decoder->verbose > 1) {
        fprintf(stderr, "%s: combined packet = ", __func__);
        bitrow_print(packet, PHILIPS_PACKETLEN * 8);
    }

    /* Correct CRC? */
    c_crc = crc4(packet, PHILIPS_PACKETLEN, 0x9, 1); /* Including the CRC nibble */
    if (0 != c_crc) {
        if (decoder->verbose) {
            fprintf(stderr, "%s: CRC failed, calculated %x\n",
                    __func__, c_crc);
        }
        return 0;
    }

    /* Message validated, now parse the data */

    /* Channel */
    channel = packet[0] & 0x0f;
    if (channel >= (sizeof(channel_map) / sizeof(channel_map[0])))
        channel = 0;
    else
        channel = channel_map[channel];

    /* Temperature */
    temp_raw = (packet[1] << 2) | (packet[2] >> 6);
    temperature = (temp_raw - 500) * 0.1;

    /* Battery status */
    battery_status = packet[PHILIPS_PACKETLEN - 1] & 0x40;

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, _X("Philips-Temperature","Philips outdoor temperature sensor"),
            "channel",       "Channel",     DATA_INT,    channel,
            "battery",       "Battery",     DATA_STRING, battery_status ? "LOW" : "OK",
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "channel",
        "battery",
        "temperature_C",
        NULL,
};

r_device philips_aj3650 = {
        .name        = "Philips outdoor temperature sensor (type AJ3650)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 2000,
        .long_width  = 6000,
//        .gap_limit   = 8000,
        .reset_limit = 30000,
        .decode_fn   = &philips_aj3650_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
