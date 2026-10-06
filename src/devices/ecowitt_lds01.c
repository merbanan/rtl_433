/** @file
    Ecowitt LDS01 laser distance / level sensor.

    Copyright (C) 2026

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Ecowitt LDS01 laser distance / tank level sensor.

- Frequency: 868.35 MHz (EU) / 915 MHz variant likely
- Modulation: FSK PCM
- Bit rate:  ~10 kbps (short = long = 100 us)
- Preamble: 0x5480 (16 bits) following the standard 0xAA... training pattern

Payload (6 bytes, transmitted MSB first after the preamble):

    Byte:   0    1    2    3    4    5
            II   II   FF   FF   DD   DD

- I: 16-bit device id (stays constant across transmissions from the same unit)
- F: 16-bit flags / status field. Observed values: 0x8f64, 0x9064.
     Exact bit meanings are not yet fully decoded; reported as a raw hex
     string so downstream users can inspect them.
- D: 16-bit distance in millimetres, big-endian (e.g. 0x01FF = 511 mm)

Example frames captured with:

    rtl_433 -f 868.35M -s 1M -X 'n=Ecowitt_LDS01,m=FSK_PCM,s=100,l=100,r=2000,preamble={16}5480'

    2a28 8f64 01ff  -> id=2a28 flags=8f64 distance=511 mm
    2a28 9064 0203  -> id=2a28 flags=9064 distance=515 mm
*/

#include "decoder.h"

static int ecowitt_lds01_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Preamble: 0x5480 (16 bits). rtl_433's search wants the sync pattern
    // as a byte array, MSB first.
    static uint8_t const preamble[] = {0x54, 0x80};

    uint8_t b[6];

    // The transmitter typically sends several repetitions of the frame in
    // one burst.  Walk every row of the bitbuffer, locate the preamble and
    // try to decode the 6 payload bytes that follow.
    for (unsigned row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned row_len = bitbuffer->bits_per_row[row];

        // Need at least: preamble (16) + payload (48) = 64 bits.
        if (row_len < 64) {
            decoder_logf(decoder, 2, __func__,
                    "row %u too short (%u bits)", row, row_len);
            continue;
        }

        unsigned start = bitbuffer_search(bitbuffer, row, 0,
                preamble, sizeof(preamble) * 8);

        if (start >= row_len) {
            decoder_logf(decoder, 2, __func__,
                    "preamble not found in row %u", row);
            continue;
        }

        // Skip past the preamble to the payload.
        unsigned pos = start + sizeof(preamble) * 8;

        if (row_len - pos < sizeof(b) * 8) {
            decoder_logf(decoder, 2, __func__,
                    "not enough bits after preamble (row %u, %u left)",
                    row, row_len - pos);
            continue;
        }

        bitbuffer_extract_bytes(bitbuffer, row, pos, b, sizeof(b) * 8);

        uint16_t id          = ((uint16_t)b[0] << 8) | b[1];
        uint16_t flags       = ((uint16_t)b[2] << 8) | b[3];
        uint16_t distance_mm = ((uint16_t)b[4] << 8) | b[5];

        // Sanity check: reject an obviously empty frame.
        if (id == 0x0000 && flags == 0x0000 && distance_mm == 0x0000) {
            decoder_log(decoder, 2, __func__, "all-zero payload, skipping");
            continue;
        }

        char id_str[5];
        char flags_str[5];
        snprintf(id_str, sizeof(id_str), "%04x", id);
        snprintf(flags_str, sizeof(flags_str), "%04x", flags);

        float distance_m = distance_mm / 1000.0f;

        /* clang-format off */
        data_t *data = data_make(
                "model",       "",             DATA_STRING, "Ecowitt-LDS01",
                "id",          "ID",           DATA_STRING, id_str,
                "flags",       "Flags",        DATA_STRING, flags_str,
                "distance_mm", "Distance",     DATA_FORMAT, "%u mm", DATA_INT, distance_mm,
                "distance_m",  "Distance",     DATA_FORMAT, "%.3f m", DATA_DOUBLE, (double)distance_m,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_ABORT_EARLY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "flags",
        "distance_mm",
        "distance_m",
        NULL,
};

r_device const ecowitt_lds01 = {
        .name        = "Ecowitt LDS01 laser distance / tank level sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 2000,
        .decode_fn   = &ecowitt_lds01_decode,
        .fields      = output_fields,
};
