/** @file
    Opel Mokka Car Key.

    Copyright (C) 2026 Vidar Madsen

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

#include "decoder.h"

/**
Opel Mokka Car Key.

Presumably a transponder of type "HITAG AES 4A NCF29A1M", so it might
very well pick up other compatible brands as well.

Only extracts key id and event type. There's no decryption of payload.

Each frame looks like this, after a preamble of 88 zeros:

    1 10000110010 11010 0000010100011111101001100000010100101011001000011100100101010110 1 000000000000000 1 10000110010 11010 0000010100011111101001100000010100101011001000011100100101010110 1
    S iiiiiiiiiii ttttt cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc E ............... S iiiiiiiiiii ttttt cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc E

- S =  1 start bit
- i = 11 bits, key ID
- t =  5 bits, packet type
- c = 64 bits, encrypted payload
- E =  1 end bit

Event type is the same for both lock and unlock (26), so the actual user
action is unknown. The key fob periodically sends a zero-filled packet
with a different type (3) as well, possibly as a proximity signal to the
vehicle.
*/

static int opel_mokka_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int found = 0;

    for (int i = 0; i < bitbuffer->num_rows; i++) {
        if (bitbuffer->bits_per_row[i] != 268) {
            continue; // Invalid length
        }

        uint8_t *b = bitbuffer->bb[i];

        // Check for zero-filled preamble. (Probably not necessary, since there's a redundant payload to verify against as well. See below.)
        if (b[0] || b[1] || b[2] || b[3] || b[4] || b[5] || b[6] || b[7] || b[8] || b[9] || b[10]) {
            continue;
        }

        uint8_t temp_id[2];
        bitbuffer_extract_bytes(bitbuffer, i, 90, temp_id, 11);
        int key_id = (temp_id[0] << 3) | (temp_id[1] >> 5);

        // Payload is sent twice, so verify that ids match
        bitbuffer_extract_bytes(bitbuffer, i, 90 + 12 * 8 + 1, temp_id, 11);
        int check_id = (temp_id[0] << 3) | (temp_id[1] >> 5);
        if (key_id != check_id) {
            continue;
        }

        // An all-zero frame trivially passes the redundant-id check above
        // (0 == 0) and would otherwise be reported as a bogus id-0 key.
        // The key id is fixed per physical fob and never actually 0; the
        // documented all-zero "proximity" packet still carries a real id
        // and just has a zero-filled code/event, so this doesn't reject it.
        if (key_id == 0) {
            continue;
        }

        int event_type = ((b[12] & 0x07) << 2) | (b[13] & 0xc0) >> 6;

        uint8_t code[8];
        bitbuffer_extract_bytes(bitbuffer, i, 90 + 17, code, 64);
        char code_str[17];
        bitrow_snprint(code, 8 * 8, code_str, sizeof(code_str));

        /* clang-format off */
        data_t *data = data_make(
                "model",    "",     DATA_STRING,    "Opel-Mokka",
                "id",       "",     DATA_INT,       key_id,
                "event",    "",     DATA_INT,       event_type,
                "code",     "",     DATA_STRING,    code_str,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);

        found++;
    }
    return found;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "event",
        "code",
        NULL,
};

r_device const opel_mokka = {
        .name        = "Opel Mokka Car Key",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 1000,
        .decode_fn   = &opel_mokka_decode,
        .fields      = output_fields,
};
