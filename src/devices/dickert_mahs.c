/** @file
    Dickert MAHS433-01 remote control

    Copyright (C) 2024 daubsi

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Dickert MAHS433-01 remote control

The Dickert MAHS433-01 remote contains a user-accessible bank of 10 dip switches labeled "1" to "10" and
each tristate dip switch can be set to one of three positions. These positions are labeled as "-" (down),
"0" (half-way up), and "+" (up). Based on the position of these switches, 59,049 (3^10) unique codes are
possible. There seems to be a model of this device "MAHS433-01" that has one button to trigger a repeating
signal for the duration it is held, and there may be a "MAHS433-04" device with 4 buttons.

There's some photos and documentation on the Dickert Electronic site: https://dickert.com/de/mahs433-01-02004600.html

Note that Cardin S466-TX2 (cardin.c) also decodes a bank of tri-state DIP switches to a "dipswitch" string,
the same key name is reused here for consistency.

The signal itself is a bit unusual. Logical bits each seem to be encoded over three symbols. A logical "1" is
encoded as "001" and a logical "0" is encoded as "011" which, although it looks like typical PWM, has each bit
encoding starting with a ASK/OOK gap, then ending with the PWM pulse. The start of the signal is a single "1"
pulse symbol.

After decoding, there are 36 logical bits. The first 20 are 10 sets of 2 bits encoding the state of the 10
tristate dip switches. A "-" state is "00", a "0" state is "01" and a "+" state is "11". "10" is never observed
and seems to be invalid. The remaining 16 bits comprise a factory code of 8 more trinary symbols, which so far
has been observed identical (0x5515) across multiple devices from the same batch.

Please see more details on https://github.com/merbanan/rtl_433/issues/2983
*/

static int dickert_pwm_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // We only expect one row per transmission
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int const num_bits = 36; // 10 dip switches (2 bits each) + 8 factory symbols (2 bits each)
    int const msg_len  = num_bits + 1; // plus the leading single "1" pulse symbol

    if (bitbuffer->bits_per_row[0] != msg_len) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[5];
    // Skip the leading "1" pulse symbol, keep exactly the 36 logical bits
    bitbuffer_extract_bytes(bitbuffer, 0, 1, b, num_bits);

    char const trinary[4] = {'-', '0', '?', '+'};

    char dip_s[10 + 1] = {0};
    char fac_s[8 + 1]  = {0};

    for (int field = 0; field < 18; field++) {
        int byte_idx = field / 4;
        int nib      = 3 - (field % 4);
        uint8_t val  = (b[byte_idx] >> (2 * nib)) & 0x3;
        char c       = trinary[val];

        if (field < 10) {
            dip_s[field] = c;
        }
        else {
            fac_s[field - 10] = c;
        }
    }

    // The first 20 logical bits (10 dip switches) as a numeric id
    int id = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "Dickert-MAHS433",
            "id",           "",             DATA_INT,    id,
            "dipswitch",    "DIP switches", DATA_STRING, dip_s,
            "facswitch",    "Factory code", DATA_STRING, fac_s,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "dipswitch",
        "facswitch",
        NULL,
};

r_device const dickert_pwm = {
        .name        = "Dickert MAHS433-01 garage door remote control",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 362,
        .long_width  = 770,
        .gap_limit   = 1064,
        .reset_limit = 12000,
        .disabled    = 1,
        .decode_fn   = &dickert_pwm_decode,
        .fields      = output_fields,
};
