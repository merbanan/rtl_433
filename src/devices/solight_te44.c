/* Solight TE44
 *
 * Generic wireless thermometer of chinese provenience, which might be sold as part of different kits.
 *
 * So far these were identified (mostly sold in central/eastern europe)
 * - Solight TE44
 * - Solight TE66
 * - EMOS E0107T
 *
 * Rated -50 C to 70 C, frequency 433,92 MHz, three selectable channels.
 *
 * ---------------------------------------------------------------------------------------------
 *
 * Data structure:
 *
 * 12 repetitions of the same 36 bit payload, 1bit zero as a separator between each repetition.
 *
 * 36 bit payload format: xxxxxxxx 10ccmmmm tttttttt 1111hhhh hhhh
 *
 * x - random key - changes after device reset - 8 bits
 * c - channel (0-2) - 2 bits
 * m - multiplier - signed integer, two's complement - 4 bits
 * t - temperature in celsius - unsigned integer - 8 bits
 * h - checksum - 8 bits
 *
 * Temperature in C = ((256 * m) + t) / 10
 *
 * ----------------------------------------------------------------------------------------------
 *
 * Copyright (C) 2017 Miroslav Oujesky
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

// NOTE: this should really not be here
int rubicson_crc_check(bitrow_t *bb);

static int solight_te44_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t id;
    uint8_t channel;
    int8_t multiplier;
    uint8_t temperature_raw;
    float temperature;
    unsigned bits = bitbuffer->bits_per_row[0];

    bitrow_t *bb = bitbuffer->bb;

    if (bits != 37)
        return 0;

    if (!rubicson_crc_check(bb))
        return 0;

    id = bb[0][0];

    channel = (uint8_t) ((bb[0][1] & 0x30) >> 4);

    multiplier = (int8_t) (bb[0][1] & 0x0F);
    // multiplier is 4bit signed value in two's complement format
    // we need to pad with 1s if it is a negative number (starting with 1)
    if ((multiplier & 0x08) > 0) {
        multiplier |= 0xF0;
    }

    temperature_raw = (uint8_t) bb[0][2];

    temperature = (float) (((256 * multiplier) + temperature_raw) / 10.0);

    data = data_make(
            "model", "", DATA_STRING, _X("Solight-TE44","Solight TE44"),
            "id", "Id", DATA_INT, id,
            "channel", "Channel", DATA_INT, channel + 1,
            "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temperature,
            "mic",           "Integrity",   DATA_STRING, "CRC",
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "temperature_C",
    NULL
};

r_device solight_te44 = {
    .name          = "Solight TE44",
    .modulation    = OOK_PULSE_PPM,
    .short_width   = 972, // short gap = 972 us
    .long_width    = 1932, // long gap = 1932 us
    .gap_limit     = 3000, // packet gap = 3880 us
    .reset_limit   = 6000,
    .decode_fn     = &solight_te44_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
