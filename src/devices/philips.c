/*
 * Philips outdoor temperature sensor -- used with various Philips clock
 * radios (tested on AJ3650)
 *
 * Not tested, but these should also work: AJ7010, AJ260 ... maybe others?
 *
 * A complete message is 112 bits:
 *      4-bit initial preamble, always 0
 *      4-bit packet separator, always 0, followed by 32-bit data packet.
 *      Packets are repeated 3 times for 108 bits total.
 *
 * 32-bit data packet format:
 *
 * 0001cccc tttttttt tt000000 0b0?ssss
 *
 * c - channel: 0=channel 2, 2=channel 1, 4=channel 3 (4 bits)
 * t - temperature in Celsius: subtract 500 and divide by 10 (10 bits)
 * b - battery status: 0 = OK, 1 = LOW (1 bit)
 * ? - unknown: always 1 in every packet I've seen (1 bit)
 * s - CRC: non-standard CRC-4, poly 0x9, init 0x1
 *
 * Pulse width:
 *      Short: 2000 us = 0
 *      Long: 6000 us = 1
 * Gap width:
 *      Short: 6000 us
 *      Long: 2000 us
 * Gap width between packets: 29000 us
 *
 * Presumably the 4-bit preamble is meant to be a sync of some sort,
 * but it has the exact same pulse/gap width as a short pulse, and
 * gets processed as data.
 *
 * Copyright (C) 2017 Chris Coffey <kpuc@sdf.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

#define PHILIPS_BITLEN       112
#define PHILIPS_PACKETLEN    4
#define PHILIPS_STARTNIBBLE  0x0

/* Map channel values to their real-world counterparts */
static const uint8_t channel_map[] = { 2, 0, 1, 0, 3 };

static int philips_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *bb;
    unsigned int i;
    uint8_t a, b, c;
    uint8_t packet[PHILIPS_PACKETLEN];
    uint8_t c_crc;
    uint8_t channel, battery_status;
    int tmp;
    float temperature;
    data_t *data;

    /* Get the time */
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
    if (channel > (sizeof(channel_map) / sizeof(channel_map[0])))
        channel = 0;
    else
        channel = channel_map[channel];

    /* Temperature */
    tmp = packet[1];
    tmp <<= 2;
    tmp |= ((packet[2] & 0xc0) >> 6);
    tmp -= 500;
    temperature = tmp / 10.0f;

    /* Battery status */
    battery_status = packet[PHILIPS_PACKETLEN - 1] & 0x40;

    data = data_make(
                     "model",         "",            DATA_STRING, _X("Philips-Temperature","Philips outdoor temperature sensor"),
                     "channel",       "Channel",     DATA_INT,    channel,
                     "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
                     "battery",       "Battery",     DATA_STRING, battery_status ? "LOW" : "OK",
                     NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *philips_output_fields[] = {
    "model",
    "channel",
    "temperature_C",
    "battery",
    NULL
};

r_device philips = {
    .name          = "Philips outdoor temperature sensor",
    .modulation    = OOK_PULSE_PWM,
    .short_width   = 2000,
    .long_width    = 6000,
    .reset_limit   = 30000,
    .decode_fn     = &philips_callback,
    .disabled      = 0,
    .fields        = philips_output_fields,
};

