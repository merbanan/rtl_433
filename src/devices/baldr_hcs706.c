/** @file
    Baldr HCS706 Pool Thermometer sensor.

    Copyright (C) 2026 Ke Wu

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Baldr HCS706 Pool Thermometer sensor.

Brand:

- Fujian Baldr Technology Co., Ltd.

Reference:

- BaldrTherm HCS706 Pool Sensor, paired with a HomGar hub, 433.92 MHz

S.a. Issue 3333

OOK PCM, MC, Invert and Reflect, the message is repeated 10 times.
Same family as Baldr HCS528ARF, but a 14 byte message instead of 11.
The MC bitstream starts with 2 leading bits before the sync byte,
so the decoder tries a few MC start offsets.

Flex:

    rtl_433 -X 'n=name,m=OOK_PCM,s=305,l=305,g=700,r=1100'

Data Layout (after MC decode, invert and reflect):

    Byte Position  0  1  2  3  4  5  6  7  8  9 10 11 12 13
    Data           SS II II II II FF FF FF FF FF FF TT TF CC

- SS:{8}  0x55, header sync word
- II:{32} Sensor ID, little endian
- FF:{48} Fixed/flag bytes, not fully decoded
  (byte 5 in {0x81, 0x88, 0x89}, byte 8 = 0x4f, byte 9 = 0x01, byte 10 = 0x0b)
- B1:{1}  Low Battery flag in byte 5 bit 0x02 = 1, Good Battery = 0
  (To be confirmed, assumption from Baldr HCS528ARF)
- TT:{12} Temperature, Fahrenheit, scale 10
  (byte 11 low 8 bits, byte 12 low nibble high 4 bits, little endian)
- CC:{8}  Checksum, addition of bytes 1 to 12.

Verified against the vendor (HomGar) cloud readings during a controlled
ice-water cooldown, 7 of 7 packets matched exactly.

*/

static int baldr_hcs706_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Find repeats
    int row = bitbuffer_find_repeated_row(bitbuffer, 4, 220);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[row] > 250) {
        return DECODE_ABORT_LENGTH;
    }

    // The MC bitstream carries 2 leading bits before the sync byte;
    // try start offsets until sync and checksum line up.
    uint8_t *b = NULL;
    bitbuffer_t decoded = {0};
    for (int start = 0; start <= 4; ++start) {
        bitbuffer_clear(&decoded);
        bitbuffer_manchester_decode(bitbuffer, row, start, &decoded, 14 * 2 * 8);
        bitbuffer_invert(&decoded);
        if (decoded.bits_per_row[0] < 14 * 8) {
            continue;
        }
        b = decoded.bb[0];
        reflect_bytes(b, 14);
        if (b[0] != 0x55) { // Header sync word
            continue;
        }
        // Checksum
        int sum = add_bytes(&b[1], 12); // header not part of the sum
        if ((sum & 0xff) != b[13]) {
            continue;
        }
        break; // sync and checksum ok
    }
    if (b == NULL || b[0] != 0x55) {
        return DECODE_ABORT_EARLY;
    }
    int sum = add_bytes(&b[1], 12);
    if ((sum & 0xff) != b[13]) {
        decoder_logf(decoder, 2, __func__, "Checksum failed %04x vs %04x", b[13], sum);
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, 14 * 8, "MSG");

    int id       = b[4] << 24 | b[3] << 16 | b[2] << 8 | b[1]; // little endian
    int bat_low  = (b[5] & 0x02) >> 1;
    int temp_raw = (b[12] & 0x0F) << 8 | b[11]; // little endian
    float temp_f = temp_raw * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING,    "Baldr-HCS706",
            "id",               "",             DATA_FORMAT,    "%08x",   DATA_INT,       id,
            "battery_ok",       "Battery",      DATA_INT,       !bat_low,
            "temperature_F",    "Temperature",  DATA_FORMAT,    "%.1f F", DATA_DOUBLE, temp_f,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_F",
        "mic",
        NULL,
};

r_device const baldr_hcs706 = {
        .name        = "Baldr HCS706 Pool Thermometer sensor",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 305,
        .long_width  = 305,
        .gap_limit   = 700,
        .reset_limit = 1100,
        .decode_fn   = &baldr_hcs706_decode,
        .fields      = output_fields,
};
