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
 * s - CRC: non-standard CRC-4, reverse-engineered using RevEng (4 bits)
 *     http://reveng.sourceforge.net
 *     width=4  poly=0x9  init=0x1  refin=false  refout=false  xorout=0x0  check=0x5  residue=0x0  name=(none)
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
 * Copyright (C) 2017 Chris Coffey (kpuc@sdf.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "util.h"

#define PHILIPS_BITLEN       112
#define PHILIPS_PACKETLEN    4
#define PHILIPS_STARTNIBBLE  0x0

/* Map channel values to their real-world counterparts */
static const uint8_t channel_map[] = { 2, 0, 1, 0, 3 };


/* philips_crc4_bit():
 * Compute CRC-4 for a byte sequence, one bit at a time.
 *
 * Note that Philips uses a custom CRC implementation for this device; see above
 * for details.
 * 
 * This function was generated using crcany (https://github.com/madler/crcany)
 * License information: "This code is under the zlib license, permitting free
 * commercial use."
 */
static unsigned philips_crc4_bit(unsigned crc, void const *mem, size_t len) 
{
    unsigned char const *data = mem;
    if (data == NULL)
        return 0x1;
    crc <<= 4;
    while (len--) {
        crc ^= *data++;
        for (unsigned k = 0; k < 8; k++)
            crc = crc & 0x80 ? (crc << 1) ^ 0x90 : crc << 1;
    }
    crc >>= 4;
    crc &= 0xf;
    return crc;
}


/* philips_crc4_rem():
 * Compute CRC-4 of the remaining high bits in the low byte of val.
 *
 * Note that Philips uses a custom CRC implementation for this device; see above
 * for details.
 * 
 * This function was generated using crcany (https://github.com/madler/crcany)
 * License information: "This code is under the zlib license, permitting free
 * commercial use."
 */
static unsigned philips_crc4_rem(unsigned crc, unsigned val, unsigned bits) 
{
    crc <<= 4;
    val &= ((1U << bits) - 1) << (8 - bits);
    crc ^= val;
    while (bits--)
        crc = crc & 0x80 ? (crc << 1) ^ 0x90 : crc << 1;
    crc >>= 4;
    crc &= 0xf;
    return crc;
}


static int philips_callback(bitbuffer_t *bitbuffer) 
{
    char time_str[LOCAL_TIME_BUFLEN];
    uint8_t *bb;
    unsigned int i;
    uint8_t a, b, c;
    uint8_t packet[PHILIPS_PACKETLEN];
    uint8_t r_crc, c_crc;
    uint8_t channel, battery_status;
    int tmp;
    float temperature;
    data_t *data;

    /* Get the time */
    local_time_str(0, time_str);

    /* Correct number of rows? */
    if (bitbuffer->num_rows != 1) {
        if (debug_output) {
            fprintf(stderr, "%s %s: wrong number of rows (%d)\n", 
                    time_str, __func__, bitbuffer->num_rows);
        }
        return 0;
    }

    /* Correct bit length? */
    if (bitbuffer->bits_per_row[0] != PHILIPS_BITLEN) {
        if (debug_output) {
            fprintf(stderr, "%s %s: wrong number of bits (%d)\n", 
                    time_str, __func__, bitbuffer->bits_per_row[0]);
        }
        return 0;
    }

    bb = bitbuffer->bb[0];

    /* Correct start sequence? */
    if ((bb[0] >> 4) != PHILIPS_STARTNIBBLE) {
        if (debug_output) {
            fprintf(stderr, "%s %s: wrong start nibble\n", time_str, __func__);
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
    if (debug_output) {
        fprintf(stderr, "%s %s: combined packet = ", time_str, __func__);
        for (i = 0; i < PHILIPS_PACKETLEN; i++) {
            fprintf(stderr, "%02x ",packet[i]);
        }
        fprintf(stderr, "\n");
    }

    /* Correct CRC? */
    r_crc = packet[PHILIPS_PACKETLEN - 1] & 0x0f; /* Last nibble in the packet */
    c_crc = philips_crc4_bit(1, packet, PHILIPS_PACKETLEN - 1); /* First three bytes */
    c_crc = philips_crc4_rem(c_crc, packet[PHILIPS_PACKETLEN - 1], 4); /* Remaining high nibble */

    if (r_crc != c_crc) {
        if (debug_output) {
            fprintf(stderr, "%s %s: CRC failed, calculated %x, received %x\n",
                    time_str, __func__, c_crc, r_crc);
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

    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "Philips outdoor temperature sensor",
                     "channel",       "Channel",     DATA_INT,    channel,
                     "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
                     "battery",       "Battery",     DATA_STRING, battery_status ? "LOW" : "OK",
                     NULL);

    data_acquired_handler(data);

    return 1;
}

static char *philips_output_fields[] = {
    "time",
    "model",
    "channel",
    "temperature_C",
    "battery",
    NULL
};

r_device philips = {
    .name          = "Philips outdoor temperature sensor",
    .modulation    = OOK_PULSE_PWM_TERNARY,
    .short_limit   = 500,
    .long_limit    = 4000,
    .reset_limit   = 30000,
    .json_callback = &philips_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = philips_output_fields,
};

