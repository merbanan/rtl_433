/** @file
    Kia Rio UB III (UB) 2011-2017 TPMS sensor and some Hyundai models too.

    Copyright (C) 2022 Lasse Mikkel Reinhold, Todor Uzunov aka teou, TTiges, 2019 Andreas
    Spiess, 2017 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify it under the terms of
    the GNU General Public License as published by the Free Software Foundation; either version
    2 of the License, or (at your option) any later version.
*/

/**
TPMS sensor for Kio Rio III (UB) 2011-2017 and some Hyundai models. Possibly other brands and
models too.

The sensors have accelerometers that sense the centripetal force in a spinning wheel and begin
to transmit data around 40 km/h. They usually keep transmitting for several minutes after
stopping, but not always; on a few rare occasions they stop instantly. Each sensor sends a
burst of 4-6 packets two times a minutes. The packets in a burst are often, but not always,
identical.

154 bits in a packet. Bit layout (leftmost bit in a field is the most significant):
    zzzzzzzzzzzzzzzz aaaa pppppppp tttttttt iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii dddddddd ccccc

Legend:
    z: 16-bit preamble = 0xed71. Must be omitted from Manchester-decoding
    a: Unknown, but 0xf in all my own readings
    p: 8-bit pressure given as PSI * 5
    t: 8-bit temperature given as Celsius + 50
    i: 32-bit Sensor ID
    d: Unknown, with different value in each packet
    c: First 5 bits of CRC. We need to append 000 to reach 8 bits. poly=0x07, init=0x76.

NOTE: I often get pressure and temperature values that are outliers (like 200 C or 10 PSI) from
all four sensors, even when CRC is OK. I don't know if all my sensors are defunct or if I have
missed something in the encoding. I have included a "raw" field to make it easier for other
users to investigate it.

NOTE: You may need to use the "-s 1000000" option of rtl_433 in order to get a clear signal.
 */

#include "decoder.h"

static int tpms_kia_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    uint8_t unknown1;
    uint8_t unknown2;
    uint8_t pressure;

    uint8_t temperature;
    uint8_t crc;
    unsigned int start_pos;
    const unsigned int preamble_length = 16;

    start_pos = bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 154 - preamble_length);
    if (start_pos - bitpos < 154 - preamble_length) {
        return DECODE_ABORT_LENGTH;
    }

    b = packet_bits.bb[0];

    unknown1    = b[0] >> 4;
    pressure    = b[0] << 4 | b[1] >> 4;
    temperature = b[1] << 4 | b[2] >> 4;
    id          = b[2] << 28 | b[3] << 20 | b[4] << 12 | b[5] << 4 | b[6] >> 4;
    unknown2    = b[6] << 8 | b[7];

    // The last 3 bits in b[8] are beyond the packet length of 154 bits. Make them 000.
    crc       = b[8] & (~0x7);
    uint8_t c = crc8(b, 8, 0x07, 0x76);
    if (crc != c) {
        return DECODE_FAIL_MIC;
    }

    char id_str[9 + 1];
    snprintf(id_str, sizeof(id_str), "%08x", id);
    char unknown1_str[2 + 1];
    snprintf(unknown1_str, sizeof(unknown1_str), "%02x", unknown1);
    char unknown2_str[3 + 1];
    snprintf(unknown2_str, sizeof(unknown2_str), "%03x", unknown2);
    char raw[9 * 2 + 1]; // 9 bytes in hex notation
    snprintf(raw, sizeof(raw), "%02x%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8]);

    float pressure_float    = pressure / 5.0;
    float temperature_float = temperature - 50.0;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Kia",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "unknown1",         "",             DATA_STRING, unknown1_str,
            "unknown2",         "",             DATA_STRING, unknown2_str,
            "pressure_PSI",     "pressure",     DATA_FORMAT, "%.1f PSI", DATA_DOUBLE, pressure_float,
            "temperature_C",    "temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, temperature_float,
            "raw",              "",             DATA_STRING, raw,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Wrapper for the Kia tpms.
@sa tpms_kia_decode()
*/
static int tpms_kia_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[2] = {0xed, 0x71};
    const int preamble_length         = 16;

    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, preamble_length)) + 154 <= bitbuffer->bits_per_row[0]) {
        ret = tpms_kia_decode(decoder, bitbuffer, 0, bitpos + preamble_length);
        if (ret > 0) {
            events += ret;
        }
        bitpos += 2;
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "unknown1",
        "unknown2",
        "pressure_PSI",
        "temperature_C",
        "raw",
        "mic",
        NULL,
};

r_device const tpms_kia = {
        .name        = "Kia TPMS (-s 1000k)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 50,
        .long_width  = 50,
        .reset_limit = 200,
        .decode_fn   = &tpms_kia_callback,
        .fields      = output_fields,
};
