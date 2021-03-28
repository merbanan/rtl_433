/** @file
    Govee Sensor support.

    * Govee Water Leak Dectector H5054.
    * Govee Door Contact Sensor B5023.


    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
https://www.govee.com/

Govee Water Leak Detector H5054:
https://www.govee.com/products/110/water-leak-detector


Govee Door Contact Sensor B5023:
https://www.govee.com/products/27/govee-door-contact-sensor
https://www.govee.com/products/154/door-open-chimes-2-pack

NOTE: The Govee Door Contact sensors only send a message when the contact
      is opened.
      Unfortunately, it does NOT send a message when the contact is closed.

*/

#include "decoder.h"

#define GOVEE_ILLEGAL_ID        (0xFFFF)
#define GOVEE_ILLEGAL_EVENT     (0xFFFF)
#define GOVEE_WATER_LEAK_SENSOR "Govee-Water-H5054"
#define GOVEE_CONTACT_SENSOR    "Govee-Contact-B5023"

static int govee_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    char *model_str = GOVEE_WATER_LEAK_SENSOR;
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
     *
     * First 2 bytes is the ID.
     *
     * The upper nibble of the next byte is unknown.
     * NOTE: This upper nibble of the Water Leak Sensor is always 0.
     *       This upper nibble of the Contact Sensor changes on different
     *       Contact sensors, so perhaps it is a continuation of the ID?
     *
     * The lower nibble plus next byte is the ACTION/EVENT.
     *
     * Last 2 bytes are unknown. They might be a CRC? Maybe a Checksum?
     */

    b = bitbuffer->bb[r];

    id = (b[0] << 8 | b[1]);
    if (id == GOVEE_ILLEGAL_ID) {
        return DECODE_ABORT_EARLY;
    }

    event = (b[2] << 8 | b[3]);
    if (event == GOVEE_ILLEGAL_EVENT) {
        return DECODE_ABORT_EARLY;
    }

    /* Strip off the upper nibble */
    event &= 0x0FFF;

    /* Figure out what event was triggered */
    if (event == 0x505) {
        event_str = "Button Press";
    }
    else if (event == 0x404) {
        event_str = "Water Leak";
    }
    else if (event == 0x39b) {
        event_str = "Batt 5 Bars";
    }
    else if (event >= 0x3b4 && event <= 0x3c4) {
        /*
         * There is a range of 4 bars here, it is unclear what each means...
         * Perhaps some sort of percentage in the 4 bar range?
         */
        event_str = "Batt 4 Bars";
    }
    else if (event == 0x3e5) {
        event_str = "Batt 3 Bars";
    }
    else if (event == 0x3e7) {
        event_str = "Batt 2 Bars";
    }
    else if (event == 0x3fe) {
        /*
         * NOTE: I used some really low/possibly going bad
         * rechargable AAA's, and got this code.
         * The sensor continuously beeped, but it did send this code out.
         * I am guessing it indicates 1 Bar.
         */
        event_str = "Batt 1 Bar";
    }
    else if (event == 0x202) {
        event_str = "Heartbeat";
    }
    else if (event == 0x180) {
        /* Only sent by the Contact sensor */
        model_str = GOVEE_CONTACT_SENSOR;
        event_str = "Open";
    }
    else {
       event_str = "Unknown";
    }

    sprintf(code_str, "%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);

    /* clang-format off */
    data = data_make(
        "model",         "",            DATA_STRING, model_str,
        "id"   ,         "",            DATA_INT,    id,
        "event",         "",            DATA_STRING, event_str,
        "code",          "Raw Code",    DATA_STRING, code_str,
        NULL);
    /* clang-format on */

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

r_device govee = {
        .name           = "Govee",
        .modulation     = OOK_PULSE_PWM,
        .short_width    = 440, // Threshold between short and long pulse [us]
        .long_width     = 940, // Maximum gap size before new row of bits [us]
        .gap_limit      = 900, // Maximum gap size before new row of bits [us]
        .reset_limit    = 9000, // Maximum gap size before End Of Message [us]
        .decode_fn      = &govee_decode,
        .fields         = output_fields,
};
