/** @file
    Neptune R900 flow meter decoder.

    Copyright (C) 2022 Jeffrey S. Ruby

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include "decoder.h"

/** @fn int neptune_r900_decode(r_device *decoder, bitbuffer_t * bitbuffer)
Neptune R900 flow meter decoder.

The product site lists E-CODER R900 amd MACH10 R900. Not sure if this decodes both.

Tested on E-CODER R900 capture files.

The device uses PPM encoding,
- 1 is encoded as 30 us pulse.
- 0 is encoded as 30 us gap.

A gap longer than 320 us is considered the end of the transmission.

The device sends a transmission every xx seconds.

A transmission starts with a preamble of 0xAA,0xAA,0xAA,0xAB,0x52,0xCC,0xD2
But, it is "zero" based, so if you insert a zero bit to the beginning of the bitstream,
the preamble is:
- 0x55,0x55,0x55,0x55,0xA9,0x66,0x69,0x65

It should be sufficient to find the start of the data after 0x55,0x55,0x55,0xA9,0x66,0x69,0x65.

Once the payload is decoded, the message is as follows:
(from https://github.com/bemasher/rtlamr/wiki/Protocol#r900-consumption-message)
- ID - 32 bits
- Unkn1 - 8 bits
- NoUse - 6 bits
- BackFlow - 6 bits    // found this to be 2 bits in my case ???
- Consumption - 24 bits
- Unkn3 - 2 bits
- Leak - 4 bits
- LeakNow - 2 bits

Some addidtional information here: https://github.com/bemasher/rtlamr/issues/29

After decoding the bitstream into 104 bits of payload, the layout appears to be:

Data layout:

    IIIIIIII IIIIIIII IIIIIIII IIIIIIII UUUUUUUU ???NNNBB CCCCCCCC CCCCCCCC CCCCCCCC UU?TTTLL EEEEEEEE EEEEEEEE EEEEEEEE

- I: 32-bit little-endian id
- U:  8-bit Unknown1
- N:  6-bit NoUse (3 bits)
- B:  2-bit backflow flag
- C: 24-bit Consumption Data
- U:  2-bit Unknown3
- T:  4-bit days of leak mapping (3 bits)
- L:  2-bit leak flag type
- E: 24-bit extra data????
*/

int const map16to6[16] = { -1, -1, -1, 0, -1, 1, 2, -1, -1, 5, 4, -1, 3, -1, -1, -1 };

static void decode_5to8(bitbuffer_t *bytes, uint8_t *base6_dec)
{
    // is there a better way to convert groups of 5 bits to groups of 8 bits?
    for (int i=0; i < 21; i++) {
        uint8_t data = base6_dec[i];
        bitbuffer_add_bit(bytes, data >> 4 & 0x01);
        bitbuffer_add_bit(bytes, data >> 3 & 0x01);
        bitbuffer_add_bit(bytes, data >> 2 & 0x01);
        bitbuffer_add_bit(bytes, data >> 1 & 0x01);
        bitbuffer_add_bit(bytes, data >> 0 & 0x01);
    }
}

static int neptune_r900_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // partial preamble and sync word shifted by 1 bit
    uint8_t const preamble[] = {0x55, 0x55, 0x55, 0xa9, 0x66, 0x69, 0x65};
    int const preamble_length = sizeof(preamble) * 8;

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_LENGTH;
    }

    // Search for preamble and sync-word
    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0, preamble, preamble_length);

    // check that (bitbuffer->bits_per_row[0]) greater than (start_pos+sizeof(preamble)*8+168)
    if (start_pos + preamble_length + 168 > bitbuffer->bits_per_row[0])
        return DECODE_ABORT_LENGTH;

    // No preamble detected
    if (start_pos == bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;

    decoder_logf(decoder, 1, __func__, "Neptune R900 detected, buffer is %d bits length", bitbuffer->bits_per_row[0]);

    // Remove preamble and sync word, keep whole payload
    uint8_t bits[21]; // 168 bits
    bitbuffer_extract_bytes(bitbuffer, 0, start_pos + preamble_length, bits, 21 * 8);

    uint8_t *bb = bitbuffer->bb[0];
    bitbuffer_t bytes = {0};
    uint8_t base6_dec[21] = {0};
    int count = 0;

    /*
     * Each group of four of these chips must be interpreted as a digit in base 6
     *             according to the following mapping:
     * 0011 -> 0
     * 0101 -> 1
     * 0110 -> 2
     * 1100 -> 3
     * 1010 -> 4
     * 1001 -> 5
    */
    // create a pair of char bit array of '0' and '1' for each base6 byte
    for (uint8_t k = start_pos+preamble_length; k < start_pos + preamble_length + 168; k=k+8) {
        uint8_t byte = bitrow_get_byte(bb, k);
        int highNibble = map16to6[(byte >> 4 & 0xF)];
        int lowNibble = map16to6[(byte & 0xF)];

        if (highNibble < 0 || lowNibble < 0)
            return DECODE_ABORT_EARLY;

        base6_dec[count] = (6 * highNibble) + lowNibble;
        count++;
    }

    // convert the base6 integers above into binary bits for decoding data
    // this reduces the 168 bits to 105 bits (104 bits??)
    // the first 80 bits are used in this decoder, the last 24 bits are decoded as extra
    decode_5to8(&bytes, base6_dec);
    uint8_t b[13]; // 104 bits
    bitbuffer_extract_bytes(&bytes, 0, 0, b, sizeof(b)*8);

    // decode the data

    // meter_id 32 bits
    uint32_t meter_id = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);
    //Unkn1 8 bits
    int unkn1 = b[4];
    //Unkn2 3 bits
    int unkn2 = b[5] >> 5;
    //NoUse 3 bits
    // 0 = 0 days
    // 1 = 1-2 days
    // 2 = 3-7 days
    // 3 = 8-14 days
    // 4 = 15-21 days
    // 5 = 22-34 days
    // 6 = 35+ days
    int nouse = ((b[5] >> 1)&0x0F) >> 1;
    //BackFlow 2 bits
    // During the last 35 days
    // 0 = none
    // 1 = low
    // 2 = high
    int backflow = b[5]&0x03;
    //Consumption 24 bits
    int consumption = (b[6] << 16) | (b[7] << 8) | (b[8]);
    //Unkn3 2 bits + 1 bit ???
    int unkn3 = b[9] >> 5;
    //Leak 3 bits
    // 0 = 0 days
    // 1 = 1-2 days
    // 2 = 3-7 days
    // 3 = 8-14 days
    // 4 = 15-21 days
    // 5 = 22-34 days
    // 6 = 35+ days
    int leak = ((b[9] >> 1)&0x0F) >> 1;
    //LeakNow 2 bits
    // During the last 24 hours
    // 0 = none
    // 1 = low (intermittent leak) water used for at least 50 of the 96 15-minute intervals
    // 2 = high (continuous leak) water use in every 15-min interval for the last 24 hours
    int leaknow = b[9]&0x03;
    // extra 24 bits ???
    char extra[7];
    snprintf(extra, sizeof(extra),"%02x%02x%02x", b[10], b[11], b[12]);

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",    DATA_STRING, "Neptune-R900",
            "id",          "",    DATA_INT,    meter_id,
            "unkn1",       "",    DATA_INT,    unkn1,
            "unkn2",       "",    DATA_INT,    unkn2,
            "nouse",       "",    DATA_INT,    nouse,
            "backflow",    "",    DATA_INT,    backflow,
            "consumption", "",    DATA_INT,    consumption,
            "unkn3",       "",    DATA_INT,    unkn3,
            "leak",        "",    DATA_INT,    leak,
            "leaknow",     "",    DATA_INT,    leaknow,
            "extra",       "",    DATA_STRING, extra,
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

    // Return 1 if message successfully decoded
    return 1;
}

/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */
static char const *const output_fields[] = {
        "model",
        "id",
        "unkn1",
        "unkn2",
        "nouse",
        "backflow",
        "consumption",
        "unkn3",
        "leak",
        "leaknow",
        "extra",
        NULL,
};


/*
 * r_device - registers device/callback. see rtl_433_devices.h
 */
r_device const neptune_r900 = {
        .name        = "Neptune R900 flow meters",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 30,
        .long_width  = 30,
        .reset_limit = 320, // a bit longer than packet gap
        .decode_fn   = &neptune_r900_decode,
        .fields      = output_fields,
};
