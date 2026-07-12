/** @file
    Decoder for Eco-Eye solar PV / grid current monitor.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for Eco-Eye solar PV / grid current monitor.

https://www.eco-eye.com/product-monitor-solar-smartpv

Transmitter unit with two current clamps (grid usage and PV/solar
generation) sending to a paired display every 4 seconds.

See https://github.com/merbanan/rtl_433/issues/1757

The transmission is FSK PCM with 200 us bit width. Center frequency is
somewhat below 433.92 MHz and drifts with temperature; tuning slightly
off-center (e.g. 433.55M) works better than dead-center.

Data layout, after the aa2dd4 sync word:

    PPPPPPPPPPPPPPPP UUUUUUUUUUUUUUUU CCCCCCCC

- P: 16 bit PV/solar generation current, centi-amps (0.01 A/count)
- U: 16 bit grid current used, centi-amps (0.01 A/count)
- C: 8 bit checksum

Both readings match the transmitter's own serial console output digit for
digit (e.g. serial "0129,0031" decodes to used=1.29 A, pv=0.31 A). No one
in the issue thread confirmed the engineering unit; centi-amps is assumed
since it is the only scale that keeps a later reading of used=2348
(reported when the reporter deliberately turned on more appliances) within
the range of a typical CT clamp (23.48 A), while deci-amps or whole amps
would put it far beyond what the clamp/circuit could plausibly read.

Checksum is a simple byte-add: b0+b1+b2+b3 == b4 (mod 256).

There is no id field; each transmitter/display pair is expected to be
alone on its channel.

A large reset_limit is required: the protocol has no clock recovery
beyond the bit period, so long runs of zero bits (e.g. used=0, gen=0,
check=0, i.e. 40 zero bits) cannot be reliably counted otherwise.
*/
static int ecoeye_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0x2d, 0xd4}; // 24 bits

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);
    start_pos += sizeof(preamble_pattern) * 8;

    if (start_pos >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "preamble not found");
        return DECODE_ABORT_EARLY;
    }

    uint8_t msg[5];
    if (start_pos + sizeof(msg) * 8 > bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 2, __func__, "message too short (%u)", bitbuffer->bits_per_row[0] - start_pos);
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof(msg) * 8);
    decoder_log_bitrow(decoder, 2, __func__, msg, sizeof(msg) * 8, "MSG");

    int sum = add_bytes(msg, 4);
    if ((sum & 0xff) != msg[4]) {
        decoder_logf(decoder, 2, __func__, "checksum fail %02x vs %02x", sum & 0xff, msg[4]);
        return DECODE_FAIL_MIC;
    }

    int pv   = (msg[0] << 8) | msg[1];
    int used = (msg[2] << 8) | msg[3];

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "EcoEye",
            "current_used_A",   "Used",         DATA_FORMAT, "%.2f A", DATA_DOUBLE, used * 0.01f,
            "current_pv_A",     "PV",           DATA_FORMAT, "%.2f A", DATA_DOUBLE, pv * 0.01f,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "current_used_A",
        "current_pv_A",
        "mic",
        NULL,
};

r_device const ecoeye = {
        .name        = "Eco-Eye solar PV/grid current monitor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 200,
        .long_width  = 200,
        .reset_limit = 8100,
        .decode_fn   = &ecoeye_decode,
        .fields      = output_fields,
};
