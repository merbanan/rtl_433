/** @file
    Ford Car Key.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/** @fn int ford_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Ford Car Key.

Manufacturer:
  - Alps Electric
  - BCS Access Systems
  - Dorman Products

Supported Models:
  - Alps (FCC ID CWTWB1U322)
  - Alps (FCC ID CWTWB1U331)
  - Alps (FCC ID CWTWB1U345)
  - Dorman (FCC ID PQTDORM03)
  - BCS Access Systems (FCC ID GQ43VT11T)

Data structure:

This transmitter uses a fixed ID with a sequence number.
The sequence number is not encrypted, since the byte 8 of the payload is incrementing
however, pressing different buttons alters the sequence. It's unclear how to reverse this.

This transmitter was previously decoded using PWM, then DMC; however, the Manchester Zero Bit is correct
and results in consistent and decodable data.

The encoding is unusual. The device ID is 4 bytes raw, and is decoded by XORing the bytes against themselves
to form a 24bit decoded device ID. Testing with 6 different remotes for multiple presses and every button combination, this was
consistent.

Data layout:

pppppppp pppppppp pp IIIIIIII SSSSSSSS CC

- p: 18 bit preamble
- I: 24 bit ID (This is 32 bits raw, and each byte is XOR'd to form a 24 bit ID)
- S: 32 bit sequence
- C: 8 bit unknown, maybe checksum or crc

Format string:

PREAMBLE: pppppppp pppppppp pp ID: hhhhhhhh SEQUENCE: bbbbbbbb bbbbbbbb bbbbbbbb bbbbbbbb UNKNOWN: bbbbbbbb

*/

#include "decoder.h"

static int ford_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int found = 0;

    for (int i = 0; i < bitbuffer->num_rows; i++) {
        if (bitbuffer->bits_per_row[i] < 80) {
            continue; // DECODE_ABORT_LENGTH
        }

        const uint8_t pattern[3] = {0x2A, 0x8A, 0x80};
        int offset = bitbuffer_search(bitbuffer, i, 0, pattern, 18) + 18;

        if (bitbuffer->bits_per_row[i] - offset < 72) {
            continue; // DECODE_ABORT_LENGTH
        }

        bitbuffer_invert(bitbuffer);

        uint8_t bytes[9];
        bitbuffer_extract_bytes(bitbuffer, i, offset, bytes, 72);

        uint32_t id = ((bytes[0] ^ bytes[1]) << 16) | ((bytes[1] ^ bytes[2]) << 8) | (bytes[2] ^ bytes[3]);
        char id_str[7];
        snprintf(id_str, sizeof(id_str), "%06X", id);

        char encrypted_str[11];
        snprintf(encrypted_str, sizeof(encrypted_str), "%02X%02X%02X%02X%02X", bytes[4], bytes[5], bytes[6], bytes[7], bytes[8]);

        /* clang-format off */
        data = data_make(
                "model",    "model",       DATA_STRING,  "Ford-CarRemote",
                "id",       "ID",          DATA_STRING,  id_str,
                "code",     "data",        DATA_STRING,  encrypted_str,
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
        "code",
        NULL,
};

r_device const ford_car_remote = {
        .name        = "Ford Car Remote",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 250,  // half-bit width is 250 us
        .gap_limit   = 4000,
        .reset_limit = 52000, // sync gap is 3500 us, preamble gap is 38400 us, packet gap is 52000 us
        .sync_width  = 8200,
        .decode_fn   = &ford_car_remote_decode,
        .fields      = output_fields,
};
