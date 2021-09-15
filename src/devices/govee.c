/** @file
    Govee Water Leak Dectector H5054, Door Contact Sensor B5023.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Govee Water Leak Dectector H5054, Door Contact Sensor B5023.

See https://www.govee.com/

Govee Water Leak Detector H5054:
https://www.govee.com/products/110/water-leak-detector

Govee Door Contact Sensor B5023:
https://www.govee.com/products/27/govee-door-contact-sensor
https://www.govee.com/products/154/door-open-chimes-2-pack

NOTE: The Govee Door Contact sensors only send a message when the contact
      is opened.
      Unfortunately, it does NOT send a message when the contact is closed.

- A data packet is 6 bytes, 48 bits.
- Bits are likely inverted (short=0, long=1)
- First 2 bytes are the ID.
- The upper nibble of byte 3 is unknown.
  This upper nibble of the Water Leak Sensor is always 0.
  This upper nibble of the Contact Sensor changes on different
  Contact sensors, so perhaps it is a continuation of the ID?
- The lower nibble of byte 3 is the ACTION/EVENT.
- Byte 4 is the ACTION/EVENT data; battery percentage gauge for event 0xC.
- Byte 5 is unknown.
- Last byte is a parity checksum.

Battery levels:

- 100 : 5 Bars
- 095 : 4 Bars
- 059 : 4 Bars
- 026 : 3 Bars
- 024 : 2 Bars
- 001 : 1 Bars

*/

#include "decoder.h"

#define GOVEE_WATER     5054
#define GOVEE_CONTACT   5023

static int govee_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int model = GOVEE_WATER;

    if (bitbuffer->num_rows < 3) {
        return DECODE_ABORT_EARLY; // truncated transmission
    }

    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 6 * 8);
    if (r < 0) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[r] > 6 * 8) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[r];

    // dump raw input code
    char code_str[13];
    sprintf(code_str, "%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);

    bitbuffer_invert(bitbuffer);

    int id = (b[0] << 8) | b[1];
    if (id == 0xffff) {
        return DECODE_ABORT_EARLY;
    }

    int event_type = b[2] & 0x0f;

    int event = (b[2] << 8) | b[3];
    if (event == 0xffff) {
        return DECODE_ABORT_EARLY;
    }

    // check nibble-parity of all data bytes
    uint8_t parity = xor_bytes(b, 5);
    parity = (parity >> 4) | (parity & 0x0f); // fold nibbles
    // add in the magic constant bits
    if (b[5] & 1)
        parity = (parity << 1) | 0xA1; // shift parity to correct field
    else
        parity = (parity << 2) | 0x42; // shift parity to correct field

    if (parity != b[5]) {
        return DECODE_FAIL_MIC;
    }

    // Only valid for event nibble 0xc
    // voltage fit value from 8 different sensor units, observed 2 to 3.1 volts
    int battery         = event_type == 0xc ? b[3] : 0; // percentage gauge
    float battery_level = battery * 0.01f;
    int battery_mv      = 1800 + 12 * battery;

    // Strip off the upper nibble
    event &= 0x0FFF;

    char *event_str;
    // Figure out what event was triggered
    if (event == 0xafa) {
        event_str = "Button Press";
    }
    else if (event == 0xbfb) {
        event_str = "Water Leak";
    }
    else if (event_type == 0xc) {
        event_str = "Battery Report";
    }
    else if (event == 0xdfd) {
        event_str = "Heartbeat";
    }
    else if (event == 0xe7f) {
        // Only sent by the Contact sensor
        model = GOVEE_CONTACT;
        event_str = "Open";
    }
    else {
       event_str = "Unknown";
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",                 DATA_COND,   model == GOVEE_WATER,   DATA_STRING, "Govee-Water",
            "model",        "",                 DATA_COND,   model == GOVEE_CONTACT, DATA_STRING, "Govee-Contact",
            "id"   ,        "",                 DATA_INT,    id,
            "battery_ok",   "Battery level",    DATA_COND,   battery, DATA_DOUBLE, battery_level,
            "battery_mV",   "Battery",          DATA_COND,   battery, DATA_FORMAT, "%d mV", DATA_INT, battery_mv,
            "event",        "",                 DATA_STRING, event_str,
            "code",         "Raw Code",         DATA_STRING, code_str,
            "mic",          "Integrity",        DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "battery_mV",
        "event",
        "code",
        "mic",
        NULL,
};

r_device govee = {
        .name        = "Govee Water Leak Dectector H5054, Door Contact Sensor B5023",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 440,  // Threshold between short and long pulse [us]
        .long_width  = 940,  // Maximum gap size before new row of bits [us]
        .gap_limit   = 900,  // Maximum gap size before new row of bits [us]
        .reset_limit = 9000, // Maximum gap size before End Of Message [us]
        .decode_fn   = &govee_decode,
        .fields      = output_fields,
};
