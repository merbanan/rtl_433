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

Data layout:

    II II ?E DD ?? XX

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

Data decoding:

    ID:8h8h ?4h EVENT:4h EVENTDATA:8h ?8h CHK:3b 4h 1b

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

#define GOVEE_WATER         5054
#define GOVEE_CONTACT       5023

#define GOVEE_H5054_BYTELEN 6
#define GOVEE_H5054_BITLEN  48

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
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);

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

    decoder_logf(decoder, 1, __func__, "Original Bytes: %02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);

    uint8_t parity = (b[5] >> 1 & 0x0F); // Shift 101PPPP1 -> 0101PPPP, then and with 0x0F so we're left with 000PPPP

    decoder_logf(decoder, 1, __func__, "Parity: %02x", parity);

    int chk = xor_bytes(b, 5);
    chk     = (chk >> 4) ^ (chk & 0xf);

    // Parity arguments were discovered using revdgst's RevSum and the data packets included at the top of this file.
    // 	 https://github.com/triq-org/revdgst
    if (chk != parity) {
        decoder_log(decoder, 1, __func__, "Parity did NOT match.");
        return DECODE_FAIL_MIC;
    }

    // Only valid for event nibble 0xc
    // voltage fit value from 8 different sensor units, observed 2 to 3.1 volts
    int battery         = event_type == 0xc ? b[3] : 0; // percentage gauge
    float battery_level = battery * 0.01f;
    int battery_mv      = 1800 + 12 * battery;

    // Strip off the upper nibble
    event &= 0x0FFF;

    char const *event_str;
    int wet = -1;
    // Figure out what event was triggered
    if (event == 0xafa) {
        event_str = "Button Press";
        // The H5054 water sensor does not send a message when it transitions from wet to dry nor does it have a
        // dedicated message to indicate that it is not wet. However, the sensor only sends a "button press" message if
        // the button is pressed while the device is dry (no button press message is sent if the button is pressed while
        // the sensor is wet). Since we know the sensor is dry when a "button press" message is received, "detect_wet:0"
        // is included in the output when the button is pressed as a workaround to allow the user to transition the
        // device back to the dry state.
        wet = 0;
    }
    else if (event == 0xbfb) {
        event_str = "Water Leak";
        wet = 1;
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
            "detect_wet",   "",                 DATA_COND,   wet >= 0, DATA_INT, wet,
            "event",        "",                 DATA_STRING, event_str,
            "code",         "Raw Code",         DATA_STRING, code_str,
            "mic",          "Integrity",        DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "battery_mV",
        "detect_wet",
        "event",
        "code",
        "mic",
        NULL,
};

r_device const govee = {
        .name        = "Govee Water Leak Detector H5054, Door Contact Sensor B5023",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 440,  // Threshold between short and long pulse [us]
        .long_width  = 940,  // Maximum gap size before new row of bits [us]
        .gap_limit   = 900,  // Maximum gap size before new row of bits [us]
        .reset_limit = 9000, // Maximum gap size before End Of Message [us]
        .decode_fn   = &govee_decode,
        .fields      = output_fields,
};

/**
Govee Water Leak Detector H5054

This is an updated decoder for devices with board versions dated circa 2021 as originally
reported in issue #2265.

Data layout:

    II II XE DD CC CC

- I: 16 bit ID, does not change with battery change
- X: 4 bit, always 0x3 for the sensors evaluated
- E: 4 bit event type
- D: 8 bit event data
- C: CRC-16/AUG-CCITT, poly=0x1021, init=0x1d0f


Event Information:

- 0x0 : Button Press
  - The event data (DD) is always 0x54 for the sensors evaluated. Unknown meaning.
- 0x1 : Battery Report
  - The event data (DD) reported for new batteries = 0x64 (decimal 100). When inserting
    older batteries, this value decreased. Looking at prior versions of the device,
    this appears to be a battery level percentage.
- 0x2 = Water Leak
  - The event data (DD) reported appears to be an incrementing counter for the event
    number. This value is reset to 00 when new batteries are inserted.

    When the first leak occurs, E=2 D=00. This value is transmitted once very 5 seconds
    until the leak is cleared (sensor dried off). The next leak events will be:

    E=2, D=01
    E=2, D=02
    E=2, D=03
    etc...

CRC Information:

The CRC was determined by using the tool CRC RevEng: https://reveng.sourceforge.io/:

    ./reveng -w16 -s aaaaaaaaaaaa bbbbbbbbbbbb etc...

where aaaaaaaaaaaa, bbbbbbbbbbbb, etc... were the unique codes collected from the
device.

*/

static int govee_h5054_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows < 3) {
        return DECODE_ABORT_EARLY;
    }

    int r = bitbuffer_find_repeated_row(bitbuffer, 3, GOVEE_H5054_BITLEN);
    if (r < 0) {
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[r] > GOVEE_H5054_BITLEN) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_invert(bitbuffer);

    uint8_t *b = bitbuffer->bb[r];

    char code_str[13];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);

    uint16_t chk = crc16(b, 6, 0x1021, 0x1d0f);
    if (chk != 0) {
        return DECODE_FAIL_MIC;
    }

    const uint16_t id        = b[0] << 8 | b[1];
    const uint8_t unk16      = (b[2] & 0xf0) >> 4;
    const uint8_t event      = b[2] & 0xf;
    const uint8_t event_data = b[3];
    const uint16_t crc_sum   = b[4] << 8 | b[5];

    decoder_logf(decoder, 1, __func__, "Original Bytes: %02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
    decoder_logf(decoder, 1, __func__, "id=%04x", id);
    decoder_logf(decoder, 1, __func__, "unk16=%x", unk16);
    decoder_logf(decoder, 1, __func__, "event=%x", event);
    decoder_logf(decoder, 1, __func__, "event_data=%02x", event_data);
    decoder_logf(decoder, 1, __func__, "crc_sum=%04x", crc_sum);

    char const *event_str;
    int wet = -1;
    int leak_num = -1;
    int battery  = -1;
    switch (event) {
    case 0x0:
        event_str = "Button Press";
        // The H5054 water sensor does not send a message when it transitions from wet to dry nor does it have a
        // dedicated message to indicate that it is not wet. However, the sensor only sends a "button press" message if
        // the button is pressed while the device is dry (no button press message is sent if the button is pressed while
        // the sensor is wet). Since we know the sensor is dry when a "button press" message is received, "detect_wet:0"
        // is included in the output when the button is pressed as a workaround to allow the user to transition the
        // device back to the dry state.
        wet = 0;
        break;
    case 0x1:
        event_str = "Battery Report";
        battery   = event_data;
        break;
    case 0x2:
        event_str = "Water Leak";
        wet = 1;
        leak_num  = event_data;
        break;
    default:
        event_str = "Unknown";
        break;
    }

    float battery_level = battery * 0.01f;
    int battery_mv      = 1800 + 12 * battery;

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",                 DATA_STRING, "Govee-Water",
            "id"   ,        "",                 DATA_INT,    id,
            "battery_ok",   "Battery level",    DATA_COND,   battery >= 0, DATA_DOUBLE, battery_level,
            "battery_mV",   "Battery",          DATA_COND,   battery >= 0, DATA_FORMAT, "%d mV", DATA_INT, battery_mv,
            "event",        "",                 DATA_STRING, event_str,
            "detect_wet",   "",                 DATA_COND,   wet >= 0, DATA_INT, wet,
            "leak_num",     "Leak Num",         DATA_COND,   leak_num >= 0, DATA_INT, leak_num,
            "code",         "Raw Code",         DATA_STRING, code_str,
            "mic",          "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

r_device const govee_h5054 = {
        .name        = "Govee Water Leak Detector H5054",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 440,  // Threshold between short and long pulse [us]
        .long_width  = 940,  // Maximum gap size before new row of bits [us]
        .gap_limit   = 900,  // Maximum gap size before new row of bits [us]
        .reset_limit = 9000, // Maximum gap size before End Of Message [us]
        .decode_fn   = &govee_h5054_decode,
        .fields      = output_fields,
};
