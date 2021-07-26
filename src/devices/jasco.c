/** @file
    Jasco/GE Choice Alert Wireless Device Decoder.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Jasco/GE Choice Alert Wireless Device Decoder.

- Frequency: 318.01 MHz

v0.1 based on the contact and water sensors Model 45131 / FCC ID QOB45131-3

bit stream is 25-27 bits

011-1011101110111011-0-1111101
011-1100111110111011-0-011011
011-1100011111111011-0-01001

with a 3 bit preamble, 16 bit ID field, 1 status bit, and then remainder unknown. Currently decoding the unknown as a status string.
Battery status does not seem to be part of the status (or at least swapping the batteries with mostly discharged ones doesn't change bit values)

*/

#include "decoder.h"

#define JASCO_MSG_BIT_LEN 26

static int jasco_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] <= JASCO_MSG_BIT_LEN-1 && bitbuffer->bits_per_row[0] >= JASCO_MSG_BIT_LEN+1){
        if (decoder->verbose > 1 && bitbuffer->bits_per_row[0] > 0) {
            fprintf(stderr, "%s: invalid bit count %d\n", __func__,
                     bitbuffer->bits_per_row[0]);
        }
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[6];
    uint32_t sensor_id = 0;
    int s_closed=0;
    int battery=0;
    data_t *data;
    char message[6]="";

    bitbuffer_extract_bytes(bitbuffer, 0, 0,b, 3);
    if (b[0] != 0x60) {
            return DECODE_FAIL_SANITY;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, 3,b, 16);
    sensor_id = (b[0] << 8)+ b[1];

    bitbuffer_extract_bytes(bitbuffer, 0, 19,b, 1);
    s_closed = (b[0]& 0x80) == 0x80;

    // Not decoded yet
    bitbuffer_extract_bytes(bitbuffer, 0, 20,b, 6);

    for (unsigned bit = 0; bit < 6; ++bit) {
        if (b[bit / 8] & (0x80 >> (bit % 8))) {
            message[bit]= '1';
        } else {
            message[bit]= '0';
        }
    }

//    bitbuffer_debug(bitbuffer);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Jasco/GE Choice Alert Security Devices",
            "id",               "Id",           DATA_INT,    sensor_id,
            "battery_ok",       "Battery",      DATA_INT,    battery,
            "status",           "Closed",       DATA_INT,    s_closed,
            "statusData",      "Status Data",  DATA_STRING, message ,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "status",
        "statusData",
        NULL,
};

r_device jasco = {
        .name        = "Jasco/GE Choice Alert Security Devices",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 248,
        .long_width  = 512,
        .reset_limit = 1548, // Maximum gap size before End Of Message
        .gap_limit   = 0,
        .tolerance   = 0,
        .sync_width  = 1540,
        .decode_fn   = &jasco_decode,
        .fields      = output_fields,

};
