/** @file
    FSK 9 byte Manchester encoded TPMS with xor checksum, 0435R.

    Copyright (C) 2021 Tomas Ebenlendr <ebik@ucw.cz>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
FSK 9 byte Manchester encoded TPMS with xor checksum, Renault 0435R.

Part no:
- Renault 40700 0435R
- VDO     S180052064Z

List of compatible Renault vehicles (from: https://www.vdo.com/media/190553/vdo-2017-tpms_2017-05-03.pdf)
- FLUENCE (L30_)
- LAGUNA III (BT0/1)
- LAGUNA III Grandtour (KT0/1)
- LATITUDE (L70_)
- MEGANE III Coupe (DZ0/1_)
- MEGANE III Grandtour (KZ0/1)
- MEGANE III Hatchback(BZ0_)
- SCÉNIC III (JZ0/1_)
- ZOE (BFM_)

Packet nibbles:

    II II II fx PP TT AA CC tt

- P = Pressure, 4/3 kPa
- I = id, 24-bit little-endian
- T = Temperature C, offset -50
- A = centrifugal acceleration, 5 m/s² (or maybe 0.5G), value of 255 means overflow
- C = Checksum, 8bit xor
- f = flags, (seen only c)
- x = flags (seen only 0), or maybe upper bits or pressure, if 340kPa is exceeded
- tt = 0x80 + measurement count (first == 0, up to 29), after 30th measuremet set to 0x00


Note: Pressure unit of 4/3 kPa is a guess, one of possible alternatives
 is 10Torr, which matches 4/3 kPa up to 0.1%. That is probably below
 the precision of the sensor anyways.

Note: Centrifugal acceleration unit guessed by following calculation:
- I have tires 195/65R15:
- tire height over wheel:       s  = 195mm * 65% = 0.12675m
- radius of wheel without tire: r₀ = 15''/2 = 0.1905m
- radius of tire:               r  = r₀ + s = 0.31725m
- cirumference of tire:         c  = 2πr = 0.31725m * 6.283186 = 1.99334m

Centrifugal acceleration at circumference of a tire (a) is related to the centrifugal
acceleration at sensor (a₀) by ratio of radius of tire and position of sensor,
which we guess is located exactly at the edge between the wheel and the tire.
    a₀ = a * r₀/r
Radial acceleration at circumference of tire can be calculated by formula
    a = v²/r where v is speed of the vehicle
Thus centrifugal acceleration at the sensor should be:
    a₀ = v² * r₀/r² = (KPH² / 3.6²) * (r₀/r²)

I get a₀ = KPH² * 0.146 for my tires. I plugged in speed obtained by OBD interface
(which matches GPS speed with less than 1% accuracy (yes it is lower than speed
displayed to driver)), and got values exactly five times greater than values
reported by the sensor for speeds under 93 kph. The sensor sends 255 when value does
not fit into 8 bits (that is for speeds above 93 kph on my tires).

*/

#include "decoder.h"

static int tpms_renault_0435r_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 160);
    // require 72 data bits
    if (packet_bits.bits_per_row[0] < 72) {
        return DECODE_ABORT_EARLY;
    }
    uint8_t *b = packet_bits.bb[0];

    // check checksum (checksum8 xor)
    int chk = xor_bytes(b, 9);
    if (chk != 0) {
        return DECODE_FAIL_MIC;
    }

    int tick     = b[8] & 0x7f;
    int has_tick = b[8] >> 7;

    // Sensor begins with has_tick = 1, and tick = 0. It sends data every 4.5s
    // and increments tick. Value tick >= 30 is never send, sensor instead
    // drops flag has_tick, and sets tick = 0 for rest of measurement session.
    // Tick counter is reset by several minutes of inactivity (vehicle stopped).
    if (b[8] && (!has_tick || tick > 30)) {
        return DECODE_FAIL_SANITY;
    }

    int flags = b[3];
    // observed always 0xc0 - FIXME: find possible combinations and reject message with impossible combination
    // to avoid confusion with other FSK manchester 9-byte sensors with 8bit xor checksum.

    int pressure_raw    = b[4];
    double pressure_kpa = pressure_raw / 0.75;
    int temp_c          = (int)b[5] - 50;
    int rad_acc         = (int)b[6] * 5;

    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x", b[0], b[1], b[2]);

    char flags_str[3];
    snprintf(flags_str, sizeof(flags_str), "%02x", flags);

    /* clang-format off */
    data_t *data = data_make(
            "model",           "",                         DATA_STRING, "Renault-0435R",
            "type",            "",                         DATA_STRING, "TPMS",
            "id",              "",                         DATA_STRING, id_str,
            "flags",           "",                         DATA_STRING, flags_str,
            "pressure_kPa",    "Pressure",                 DATA_FORMAT, "%.1f kPa",  DATA_DOUBLE, (double)pressure_kpa,
            "temperature_C",   "Temperature",              DATA_FORMAT, "%.0f C",    DATA_DOUBLE, (double)temp_c,
            "centrifugal_acc", "Centrifugal Acceleration", DATA_FORMAT, "%.0f m/s2", DATA_DOUBLE, (double)rad_acc,
            "mic",             "",                         DATA_STRING, "CRC",
            "has_tick",        "",                         DATA_INT,    has_tick,
            "tick",            "",                         DATA_INT,    tick - 0x80*(1-has_tick), //set to negative value when has_tick == 0 (invert bit 7)
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_renault_0435r_decode() */
static int tpms_renault_0435r_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is 55 55 55 56 (inverted: aa aa aa a9)
    uint8_t const preamble_pattern[2] = {0xaa, 0xa9}; // 16 bits

    int ret    = 0;
    int events = 0;

    bitbuffer_invert(bitbuffer);

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                        preamble_pattern, 16)) +
                        160 <=
                bitbuffer->bits_per_row[row]) {
            ret = tpms_renault_0435r_decode(decoder, bitbuffer, row, bitpos + 16);
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
        "pressure_kPa",
        "temperature_C",
        "centrifugal_acc",
        "mic",
        "has_tick",
        "tick",
        NULL,
};

r_device const tpms_renault_0435r = {
        .name        = "Renault 0435R TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 12-13 samples @250k
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_renault_0435r_callback,
        .fields      = output_fields,
};
