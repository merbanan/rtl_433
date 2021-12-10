/** @file
    Govee Water Leak Detector H5054, Door Contact Sensor B5023.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Govee Water Leak Detector H5054, Door Contact Sensor B5023.

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
- Last byte contains the parity bits in index 2-6 (101PPPP1).
  The parity checksum using CRC8 against the first 5 bytes

Battery levels:

- 100 : 5 Bars
- 095 : 4 Bars
- 059 : 4 Bars
- 026 : 3 Bars
- 024 : 2 Bars
- 001 : 1 Bars

Raw data used to select checksum algorithm (after inverting to match used data):

    Binary Data: 01101111 00111010 11111010 11111010 11111000 10101111
    Parity value from last byte: 0111

    Binary Data: 01101110 00011001 11111010 11111010 11111000 10101111
    Parity value from last byte: 0111

    Binary Data: 01011100 01100110 11111010 11111010 11111000 10111101
    Parity value from last byte: 1110

    Binary Data: 01101101 00011110 11111010 11111010 11111000 10100111
    Parity value from last byte: 0011

    Binary Data: 01100111 11111001 11111010 11111010 11111000 10100001
    Parity value from last byte: 0000

    Binary Data: 01101110 00101101 11111010 11111010 11111000 10100001
    Parity value from last byte: 0000

    Binary Data: 01011100 00000111 11111010 11111010 11111000 10110011
    Parity value from last byte: 1001

    Binary Data: 01101110 01000010 11111010 11111010 11111000 10110011
    Parity value from last byte: 1001

    Binary Data: 01101110 00111010 11111010 11111010 11111000 10101101
    Parity value from last byte: 0110

    Binary Data: 00100011 00000011 11111100 01001101 11111100 10110111
    Parity value from last byte: 1011

    Binary Data: 00100011 00000011 11111100 01000111 11111100 10100011
    Parity value from last byte: 0001

    Binary Data: 00100011 00000011 11111010 11111010 11111000 10101011
    Parity value from last byte: 0101

    Binary Data: 00011001 01010111 11111100 01001110 11111100 10100001
    Parity value from last byte: 0000

    Binary Data: 00110001 00010010 11111100 01000110 11111100 10100111
    Parity value from last byte: 0011

    Binary Data: 00110001 00010010 11111101 11111101 11111100 10100101
    Parity value from last byte: 0010

    Binary Data: 00110001 00010010 11111010 11111010 11111000 10101101
    Parity value from last byte: 0110

    Binary Data: 01010110 00010100 11111010 11111010 11111000 10100011
    Parity value from last byte: 0001

RevSum input for parity (first 5 bytes, and the parity extracted from the last byte):

    0x6f, 0x3a, 0xfa, 0xfa, 0xf8, 0x07
    0x6e, 0x19, 0xfa, 0xfa, 0xf8, 0x07
    0x5c, 0x66, 0xfa, 0xfa, 0xf8, 0x0e
    0x6d, 0x1e, 0xfa, 0xfa, 0xf8, 0x03
    0x67, 0xf9, 0xfa, 0xfa, 0xf8, 0x00
    0x6e, 0x2d, 0xfa, 0xfa, 0xf8, 0x00
    0x5c, 0x07, 0xfa, 0xfa, 0xf8, 0x09
    0x6e, 0x42, 0xfa, 0xfa, 0xf8, 0x09
    0x6e, 0x3a, 0xfa, 0xfa, 0xf8, 0x06
    0x23, 0x03, 0xfc, 0x4d, 0xfc, 0x0b
    0x23, 0x03, 0xfc, 0x47, 0xfc, 0x01
    0x23, 0x03, 0xfa, 0xfa, 0xf8, 0x05
    0x19, 0x57, 0xfc, 0x4e, 0xfc, 0x00
    0x31, 0x12, 0xfc, 0x46, 0xfc, 0x03
    0x31, 0x12, 0xfd, 0xfd, 0xfc, 0x02
    0x31, 0x12, 0xfa, 0xfa, 0xf8, 0x06
    0x56, 0x14, 0xfa, 0xfa, 0xf8, 0x01

*/

#include "decoder.h"

#define GOVEE_WATER     5054
#define GOVEE_CONTACT   5023

static int govee_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int model_num = GOVEE_WATER;

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

    if (decoder->verbose) {
        fprintf(stderr, "Original Bytes: %02x%02x%02x%02x%02x%02x\n", b[0], b[1], b[2], b[3], b[4], b[5]);
    }

    uint8_t parity = (b[5] >> 1 & 0x0F); // Shift 101PPPP1 -> 0101PPPP, then and with 0x0F so we're left with 000PPPP

    if (decoder->verbose) {
        fprintf(stderr, "Parity: %02x\n", parity);
    }

    int chk = xor_bytes(b, 5);
    chk     = (chk >> 4) ^ (chk & 0xf);

    // Parity arguments were discovered using revdgst's RevSum and the data packets included at the top of this file.
    // 	 https://github.com/triq-org/revdgst
    if (chk != parity) {
        if (decoder->verbose) {
            fprintf(stderr, "Parity did NOT match.");
        }
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
        model_num = GOVEE_CONTACT;
        event_str = "Open";
    }
    else {
       event_str = "Unknown";
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",                 DATA_COND,   model_num == GOVEE_WATER,   DATA_STRING, "Govee-Water",
            "model",        "",                 DATA_COND,   model_num == GOVEE_CONTACT, DATA_STRING, "Govee-Contact",
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
