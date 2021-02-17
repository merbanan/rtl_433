/** @file
    Govee Water Leak Dectector H5054.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Govee Water Leak Detector H5054.

https://www.govee.com/
https://www.govee.com/products/110/water-leak-detector

*/

#include "decoder.h"

#define GOVEE_WATER_DETECTOR_ILLEGAL_ID    (0xFFFF)
#define GOVEE_WATER_DETECTOR_ILLEGAL_EVENT (0xFFFF)


static int govee_water_h5054_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int r;
    uint16_t id;
    uint16_t event;
    char *event_str;
    char code_str[13];

    if (bitbuffer->num_rows < 3) {
        return DECODE_ABORT_EARLY; // truncated transmission
    }

    r = bitbuffer_find_repeated_row(bitbuffer, 3, 24);
    if (r < 0) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[r] > 6*8) {
        return DECODE_ABORT_LENGTH;
    }

    /*
     * Payload is 6 bytes.
     * First 2 bytes is the ID.
     * Middle 2 bytes is the ACTION/EVENT
     * Last 2 bytes are unknown. They might be a CRC? Maybe a Checksum?
     */

    b = bitbuffer->bb[r];

    id = (b[0] << 8 | b[1]);
    if (id == GOVEE_WATER_DETECTOR_ILLEGAL_ID) {
        return DECODE_ABORT_EARLY;
    }

    event = (b[2] << 8 | b[3]);
    if (event == GOVEE_WATER_DETECTOR_ILLEGAL_EVENT) {
        return DECODE_ABORT_EARLY;
    }

    /* What event was triggered? */
    if (event == 0x0505) {
        event_str = "Button Press";
    }
    else if (event == 0x0404) {
        event_str = "Water Leak";
    }
    else if (event == 0x039b) {
        event_str = "Batt 5 Bars";
    }
    else if (event >= 0x03b4 && event <= 0x03c4) {
        /*
         * There is a range of 4 bars here, it is unclear what each means...
         * Perhaps some sort of percentage in the 4 bar range?
         */
        event_str = "Batt 4 Bars";
    }
    else if (event == 0x03e5) {
        event_str = "Batt 3 Bars";
    }
    else if (event == 0x03e7) {
        event_str = "Batt 2 Bars";
    }
    else if (event == 0x03fe) {
        /*
         * NOTE: I used some really low/possibly going bad
         * rechargable AAA's, and got this code.
         * The sensor continuously beeped, but it did send this code out.
         * I am guessing it indicates 1 Bar.
         */
        event_str = "Batt 1 Bar";
    }
    else {
       event_str = "Unknown";
    }

    sprintf(code_str, "%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);

    data = data_make(
        "model",         "",            DATA_STRING, _X("Water-H5054","Water detector H5054"),
        "id"   ,         "",            DATA_INT, id,
        "event",         "",            DATA_STRING, event_str,
        "code",          "Raw Code",    DATA_STRING, code_str,
        NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "event",
    "code",
    NULL,
};

r_device govee_water_h5054 = {
    .name           = "Govee Water Leak Detector H5054",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 440, // Threshold between short and long pulse [us]
    .long_width     = 940, // Maximum gap size before new row of bits [us]
    .gap_limit      = 900, // Maximum gap size before new row of bits [us]
    .reset_limit    = 9000, // Maximum gap size before End Of Message [us]
    .decode_fn      = &govee_water_h5054_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
