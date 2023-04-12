/** @file
    Auriol AFT 77 B2 sensor protocol.

    Copyright (C) 2021 P. Tellenbach

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int auriol_aft77_b2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Auriol AFT 77 B2 protocol. The sensor can be bought at Lidl.

The sensor sends 68 bits at least 3 times, before the packets are 9 sync pulses
of 1900us length.
The packets are ppm modulated (distance coding) with a pulse of ~488 us
followed by a short gap of ~488 us for a 0 bit or a long ~976 us gap for a
1 bit, the sync gap is ~1170 us.

The data is grouped in 17 nibbles

    [prefix] [0x05] [0x0C] [id0] [id1] [0x00] [flags] [sign] [temp0] [temp1] [temp2]
    [0x00] [0x00] [sum] [sum] [lsrc] [lsrc]

Bitbuffer example from rtl_433 -a:

    [00] { 0}                            :
    [01] { 0}                            :
    [02] { 0}                            :
    [03] { 0}                            :
    [04] { 0}                            :
    [05] { 0}                            :
    [06] { 0}                            :
    [07] { 0}                            :
    [08] { 0}                            :
    [09] {68} a5 cf 80 20 17 30 0c ac 90
    [10] { 0}                            :

- prefix: 4 bit fixed 1010 (0x0A) ignored when calculating the checksum and lsrc
- id: 8 bit a random id that is generated when the sensor starts
- flags(1): was set at first start and reset after a restart
- flags(3): might be the battery status (not yet decoded)
- sign(3): is 1 when the reading is negative
- temp: a BCD number scaled by 10, 175 is 17.5C
- sum: 8 bit sum of the previous bytes
- lsrc: Galois LFSR, bits reflected, gen 0x83, key 0xEC

*/

#include "decoder.h"

#define GEN 0x83
#define KEY 0xEC

#define LEN 8

static uint8_t lsrc(uint8_t frame[], int len)
{
    uint8_t result = 0;
    uint8_t key    = KEY;

    for (int i = 0; i < len; i++) {
        uint8_t byte = frame[i];

        for (uint8_t mask = 0x80; mask > 0; mask >>= 1) {
            if ((byte & mask) != 0)
                result ^= key;

            if ((key & 1) != 0)
                key = (key >> 1) ^ GEN;
            else
                key = (key >> 1);
        }
    }

    return result;
}

static int search_row(bitbuffer_t *bitbuffer)
{
    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] == 68)
            return row;
    }

    return -1;
}

static int auriol_aft77_b2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;

    // Search a suitable row in the bit buffer
    int row = search_row(bitbuffer);

    // Check if found
    if (row == -1)
        return DECODE_ABORT_EARLY;

    uint8_t *ptr = bitbuffer->bb[row];

    // Check the prefix
    if (*ptr != 0xA5)
        return DECODE_ABORT_EARLY;

    uint8_t frame[LEN];

    // Drop the prefix and align the bytes
    for (int i = 0; i < LEN; i++)
        frame[i] = (ptr[i] << 4) | (ptr[i + 1] >> 4);

    // Check the sum
    if ((uint8_t)add_bytes(frame, 6) != frame[6])
        return DECODE_FAIL_MIC;

    // Check the lsrc
    if (lsrc(frame, 6) != frame[7])
        return DECODE_FAIL_MIC;

    int id = frame[1];

    int temp_raw = (ptr[4] >> 4) * 100 + (ptr[4] & 0x0F) * 10 + (ptr[5] >> 4);

    if ((ptr[3] & 0x08) != 0)
        temp_raw = -temp_raw;

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, "Auriol-AFT77B2",
            "id",            "",            DATA_INT, id,
            "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_raw * 0.1,
            "mic",              "Integrity",         DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "mic",
        NULL,
};

r_device const auriol_aft77b2 = {
        .name        = "Auriol AFT 77 B2 temperature sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 500,
        .long_width  = 920,
        .gap_limit   = 1104,
        .reset_limit = 2275,
        .decode_fn   = &auriol_aft77_b2_decode,
        .fields      = output_fields,
};
