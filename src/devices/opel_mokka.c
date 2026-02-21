/** @file
    Opel Mokka Car Key.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Opel Mokka Car Key.

Presumably a transponder of type "HITAG AES 4A NCF29A1M", so it might
very well pick up other compatible brands as well.

Only extracts key id and event type. There's no decryption of payload.

Event type is the same for both lock and unlock (26), so the actual user
action is unknown. The key fob periodically sends a zero-filled packet
with a different type (3) as well. Can be used for presence tracking, maybe.
*/

#include "decoder.h"

static int opel_mokka_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *bytes;
    int found = 0;
    int key_id, event_type;
    uint8_t temp_id[2];
    uint8_t code[8];
    char code_str[17];

    for (int i = 0; i < bitbuffer->num_rows; i++) {
        if (bitbuffer->bits_per_row[i] != 268) {
            continue; // Invalid length
        }

        bytes = bitbuffer->bb[i];

        // Check for zero-filled preamble. (Probably not necessary, since there's a redundant payload to verify against as well. See below.)
        if (bytes[0] || bytes[1] || bytes[2] || bytes[3] || bytes[4] || bytes[5] || bytes[6] || bytes[7] || bytes[8] || bytes[9] || bytes[10]) {
            continue;
        }

        bitbuffer_extract_bytes(bitbuffer, i, 90, temp_id, 11);
        key_id = ((temp_id[0] << 8) | temp_id[1]) >> 5;

        // Payload is sent twice, so verify that ids match
        bitbuffer_extract_bytes(bitbuffer, i, 90 + 12*8 + 1, temp_id, 11);
        int check_id = ((temp_id[0] << 8) | temp_id[1]) >> 5;
        if (key_id != check_id) {
            continue;
        }

        event_type = (((bytes[12] & 0x07) << 8) | (bytes[13] & 0xc0)) >> 6;
        
        bitbuffer_extract_bytes(bitbuffer, i, 90 + 17, code, 64);
        snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x%02x%02x",
            code[0], code[1], code[2], code[3], code[4], code[5], code[6], code[7]);

        /* clang-format off */
        data = data_make(
                "model", "model", DATA_STRING, "OpelMokka",
                "id",    "id",    DATA_INT,    key_id,
                "type",  "type",  DATA_INT,    event_type,
                "code",  "data",  DATA_STRING, code_str,
                NULL);
        decoder_output_data(decoder, data);
        /* clang-format on */

        found++;
    }
    return found;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "type",
    "code",
    NULL,
};

r_device const opel_mokka = {
    .name        = "Opel Mokka Car Key",
    .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
    .short_width = 100,
    .long_width  = 100,
    .reset_limit = 1000,
    .decode_fn   = &opel_mokka_callback,
    .fields      = output_fields,
};
