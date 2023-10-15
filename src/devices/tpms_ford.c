/** @file
    FSK 8 byte Manchester encoded TPMS with simple checksum.

    Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
FSK 8 byte Manchester encoded TPMS with simple checksum.
Seen on Ford Fiesta, Focus, Kuga, Escape, Transit...

Seen on 315.00 MHz (United States).

Seen on 433.92 MHz.
Likely VDO-Sensors, Type "S180084730Z", built by "Continental Automotive GmbH".

Typically a transmission is sent 4 times.  Sometimes the T/P values
differ (slightly) among those.

Sensor has 3 modes:
  moving: while being driven
  atrest: once after stopping, and every 6h thereafter (for months)
  learn: 12 transmissions, caused by using learn tool

Packet nibbles:

    II II II II PP TT FF CC

- I = ID
- P = Pressure, as PSI * 4
- T = Temperature, as C + 56, except:
      When 0x80 is on, value is not temperature, meaning the full 8
      bits is not temperature, and the lower 7 bits is also not
      temperature.  Pattern of low 7 bits in this case seems more like
      codepoints than a measurement.
- F = Flags:
      0x80 not seen
      0x40 ON for vehicle moving
        Is strongly correlated with 0x80 being set in TT
      0x20: 9th bit of pressure.  Seen on Transit very high pressure, otherwise not.
      0x10: not seen

      0x08: ON for learn
      0x04: ON for moving (0x08 and 0x04 both OFF for at rest)
      0x02: ~always NOT 0x01 (meaning of 0x3 not understood, but MOVING
            tends to have 0x02)
      0x01: about 19% of samples
- C = Checksum, SUM bytes 0 to 6 = byte 7
*/

#include "decoder.h"

static int tpms_ford_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    int code;
    float pressure_psi;
    int temperature_c, temperature_valid;
    int psibits;
    int moving;
    int learn;
    int unknown;
    int unknown_3;

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 160);

    // require 64 data bits
    if (packet_bits.bits_per_row[0] < 64) {
        return 0;
    }
    b = packet_bits.bb[0];

    if (((b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6]) & 0xff) != b[7]) {
        return 0;
    }

    id = (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];

    /* Extract and log code to aid in debugging. */
    code = b[4] << 16 | b[5] << 8 | b[6];

    /*
     * Formula is a combination of regression and plausible, observed
     * from roughly 31 to 36 psi.  (The bit at byte6-0x20 is shifted
     * to 0x100.)
     */
    psibits      = (((b[6] & 0x20) << 3) | b[4]);
    pressure_psi = psibits * 0.25f;

    /*
     * Working theory is that temperature bits are temp + 56,
     * encoding -56 to 71 C.  Validated as close around 15 C.
     */
    if ((b[5] & 0x80) == 0x80) {
        temperature_valid = 0;
        /* Avoid uninitialized warning due to DATA_COND. */
        temperature_c = -1000.0;
    }
    else {
        temperature_valid = 1;
        temperature_c     = (b[5] & 0x7f) - 56;
    }

    /*
     * Set up syndrome of unexpected bits.  The point is to have a
     * variable unknown which is zero if this packet matches the
     * code's understanding, and to be non-zero if anything is unusual,
     * to aid finding logged packets for manual study.
     */
    unknown = 0;

    /* Examine moving, learn and normal bits. */
    learn = moving = 0;
    switch (b[6] & 0x4c) {
    case 0x8:
        /* In response to learn tool */
        learn = 1;
        break;

    case 0x4:
        /* At rest. */
        break;

    case 0x44:
        /* Moving. */
        moving = 1;
        break;

    default:
        /*
             * These three bits taken together do not match a known
             * pattern.  Therefore set all of them as the unknown
             * syndrome.
             */
        unknown = (b[6] & 0x4c);
        break;
    }

    /*
     * We've accounted for
     * 0x40(moving) 0x20(temp) 0x8(learn) 04(normal)
     * 0x3(separate, next)
     * so that leaves 0x80 and 0x10, which are expected to be 0.
     */
    unknown |= (b[6] & 0x90);

    /* Low-order 2 bits are variously 01, 10. */
    unknown_3 = b[6] & 0x3;

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%08x", id);
    char code_str[7];
    snprintf(code_str, sizeof(code_str), "%06x", code);
    char unknown_str[3];
    snprintf(unknown_str, sizeof(unknown_str), "%02x", unknown);
    char unknown_3_str[2];
    snprintf(unknown_3_str, sizeof(unknown_3_str), "%01x", unknown_3);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Ford",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "pressure_PSI",     "Pressure",     DATA_FORMAT, "%.2f PSI", DATA_DOUBLE, pressure_psi,
            "temperature_C",    "Temperature",  DATA_COND, temperature_valid, DATA_FORMAT, "%.1f C",   DATA_DOUBLE, (float)temperature_c,
            "moving",           "Moving",       DATA_INT,    moving,
            "learn",            "Learn",        DATA_INT,    learn,
            "code",             "",             DATA_STRING, code_str,
            "unknown",          "",             DATA_STRING, unknown_str,
            "unknown_3",        "",             DATA_STRING, unknown_3_str,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_ford_decode() */
static int tpms_ford_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is 55 55 55 56 (inverted: aa aa aa a9)
    uint8_t const preamble_pattern[2] = {0xaa, 0xa9}; // 16 bits

    int row;
    unsigned bitpos;
    int ret    = 0;
    int events = 0;

    bitbuffer_invert(bitbuffer);

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                preamble_pattern, 16)) + 144 <=
                bitbuffer->bits_per_row[row]) {
            ret = tpms_ford_decode(decoder, bitbuffer, row, bitpos + 16);
            if (ret > 0)
                events += ret;
            bitpos += 15;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_PSI",
        "temperature_C",
        "moving",
        "learn",
        "code",
        "unknown",
        "unknown_3",
        "mic",
        NULL,
};

r_device const tpms_ford = {
        .name        = "Ford TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 12-13 samples @250k
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_ford_callback,
        .fields      = output_fields,
};
