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

#include "data.h"
#include "rtl_433.h"
#include "util.h"

static int solight_te44_callback(bitbuffer_t *bitbuffer) {

    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    uint8_t id;
    uint8_t channel;
    int8_t multiplier;
    uint8_t temperature_raw;
    float temperature;

    bitrow_t *bb = bitbuffer->bb;

    // simple payload structure check (as the checksum algorithm is still unclear)
    if (bitbuffer->num_rows != 12) {
        return 0;
    }

    for (int i = 0; i < 12; i++) {
        int bits = i < 11 ? 37 : 36; // last line does not contain single 0 separator

        // all lines should have correct length
        if (bitbuffer->bits_per_row[i] != bits) {
            return 0;
        }

        // all lines should have equal content
        // will work also for the last, shorter line, as the separating bit is allways 0 anyway
        if (i > 0 && 0 != memcmp(bb[i], bb[i - 1], BITBUF_COLS)) {
            return 0;
        }
    }

    local_time_str(0, time_str);

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

    data = data_make("time", "", DATA_STRING, time_str,
                     "model", "", DATA_STRING, "Solight TE44",
                     "id", "Id", DATA_INT, id,
                     "channel", "Channel", DATA_INT, channel + 1,
                     "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temperature,
                     NULL);
    data_acquired_handler(data);

    return 1;

}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    "temperature_C",
    NULL
};

r_device solight_te44 = {
    .name          = "Solight TE44",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = 1500, // short gap = 972 us
    .long_limit    = 3000, // long gap = 1932 us
    .reset_limit   = 6000, // packet gap = 3880 us
    .json_callback = &solight_te44_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields,
};