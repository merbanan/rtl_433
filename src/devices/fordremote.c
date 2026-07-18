/** @file
    Ford Car Key.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Ford Car Key.

Identifies event, but does not attempt to decrypt rolling code...
Note: this used to have a broken PWM decoding, but is now proper DMC.
The output changed and the fields are very likely not as intended.

    [00] {1} 80 : 1
    [01] {9} 00 80 : 00000000 1
    [02] {1} 80 : 1
    [03] {78} 03 e0 01 e4 e0 90 52 97 39 60

*/

#include "decoder.h"

static int fordremote_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // find a 78 bit payload row repeated at least twice (real transmissions
    // send several identical repeats; this reports each capture only once)
    int r = bitbuffer_find_repeated_row(bitbuffer, 2, 78);
    if (r < 3) {
        return DECODE_ABORT_LENGTH;
    }

    // expect {1} {9} {1} preamble immediately before the payload row
    if (bitbuffer->bits_per_row[r - 3] != 1 || bitbuffer->bits_per_row[r - 1] != 1
            || bitbuffer->bits_per_row[r - 2] != 9 || bitbuffer->bb[r - 2][0] != 0) {
        return DECODE_ABORT_EARLY;
    }

    decoder_log_bitbuffer(decoder, 1, __func__, bitbuffer, "");

    uint8_t *bytes = bitbuffer->bb[r];
    int device_id  = (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
    int code       = bytes[7];

    /* clang-format off */
    data_t *data = data_make(
            "model",    "model",        DATA_STRING, "Ford-CarRemote",
            "id",       "device-id",    DATA_INT,    device_id,
            "code",     "data",         DATA_INT,    code,
            NULL);
    decoder_output_data(decoder, data);
    /* clang-format on */

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "code",
        NULL,
};

r_device const fordremote = {
        .name        = "Ford Car Key",
        .modulation  = OOK_PULSE_DMC,
        .short_width = 250,  // half-bit width is 250 us
        .long_width  = 500,  // bit width is 500 us
        .reset_limit = 55000, // sync gap is 3500 us, preamble gap is 38400 us, packet gap is 52000 us
        .tolerance   = 50,
        .decode_fn   = &fordremote_callback,
        .fields      = output_fields,
        .disabled    = 1, // does not attempt to decrypt the rolling code, id/code semantics are unconfirmed
};
