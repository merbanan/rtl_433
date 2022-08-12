/** @file
    SRSmith Pool Light Remote Control, Model #SRS-2C-TX

    Copyright (C) 2022 gcohen55

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
#include "util.h"
//#include <inttypes.h>
//#include <string.h>

#define BUTTON_ID_ONE 0x0d
#define BUTTON_ID_TWO 0x1f
#define BUTTON_ID_S   0x07
#define BUTTON_ID_M   0x0b

/**
SRSmith Pool Light Remote Control, Model #SRS-2C-TX

The SR Smith remote control sends broadcasts of ~144 bits and it comes in shifted (similar to the Maverick XR30 BBQ Sensor)
- Frequency: 915MHz

Data Layout:
    PPPP WWWW S UUUU C B T PP

- P: 32 bit preamble (0xaaaaaaaa; 7 or 8 bits shifted left for analysis)
- W: 32 bit sync word (0xd391d391)
- S: 8 bit size (so far I've only seen 0x07)
- U: 32 bit unknown (I always see 0x01fffff5 here)
- C: 8 bit pin code is located in the bottom nibble of this byte, inverted and reversed.
- B: 8 bit contains the ID of the button that was pushed on the remote
- T: 8 bit CRC-8, poly 1, init 1 from the 16 bytes that we don't know (U) until the button that was pressed (B)
- P: 16 bit CRC-16, poly 0x8005, init 0xFFFF, of the packet from the size (S) until the CRC-8 (T)

Format String:
    PRE:32h SYNC: 32h SIZE: hh UNSURE:32h | UNSURE: 4b | PIN ~^4b |  BTN: hh | CRC-8: hh | CRC-16: hhhh

Capture raw but often misshifted :| data. if you want to work with this data, throw it into bitbench and shift it over until you see A's instead of 5's
    -f 915000000 -X n=SRSmith,m=FSK_PCM,s=100,l=100,r=4096,match=d391d391
*/

static int srsmith_pool_srs_2c_tx_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;

    if (bitbuffer->num_rows != 1)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[0] != 144)
        return DECODE_ABORT_LENGTH;

    // this is heavily derived from Maverick XR30 BBQ Sensor
    uint8_t b[18];

    // check for correct preamble/sync (0xaaaaaad391d391)
    if (bitbuffer->bb[0][0] == 0x55) {
        bitbuffer_extract_bytes(bitbuffer, 0, 7, b, 18 * 8); // shift in case first bit was not received properly
    }
    else if (bitbuffer->bb[0][0] == 0xaa) {
        bitbuffer_extract_bytes(bitbuffer, 0, 8, b, 18 * 8); // shift in case first bit was received properly
    }
    else {
        return DECODE_ABORT_EARLY; // preamble/sync missing
    }
    if (b[0] != 0xaa || b[1] != 0xaa || b[2] != 0xaa || b[3] != 0xd3 || b[4] != 0x91 || b[5] != 0xd3 || b[6] != 0x91)
        return DECODE_ABORT_EARLY; // preamble/sync missing

    int totallength = bitbuffer->bits_per_row[0];
    int packetsize  = (int)b[7];

    // packet actually starts at b[8]
    uint32_t unknown_field = ((uint32_t)b[8] << 24) | (b[9] << 16) | (b[10] << 8) | (b[11]); // not sure what these four bytes are.

    // get xmitted pin (byte 12, inverted, reversed, bottom nibble)
    uint8_t pin_container          = b[12];
    uint8_t inverted_pin_container = ~pin_container;                   // invert the pin
    uint8_t reversed_pin           = reverse8(inverted_pin_container); // reverse the bits

    // convert just the first four bits to binary string
    char pin_string[5] = {'\0'};
    snprintf(pin_string, sizeof(pin_string), "%d%d%d%d",
            (reversed_pin & 0x80 ? 1 : 0),
            (reversed_pin & 0x40 ? 1 : 0),
            (reversed_pin & 0x20 ? 1 : 0),
            (reversed_pin & 0x10 ? 1 : 0));

    // button that was pushed
    uint8_t button_id = b[13];

    // get label
    char button_string[50] = {'\0'};

    switch (button_id) {
    case BUTTON_ID_ONE:
        snprintf(button_string, sizeof(button_string), "On/Off Channel 1");
        break;
    case BUTTON_ID_TWO:
        snprintf(button_string, sizeof(button_string), "On/Off Channel 2");
        break;
    case BUTTON_ID_S:
        snprintf(button_string, sizeof(button_string), "Color Sync");
        break;
    case BUTTON_ID_M:
        snprintf(button_string, sizeof(button_string), "ON/OFF Control - M");
        break;
    default:
        snprintf(button_string, sizeof(button_string), "Unknown");
        break;
    }

    // grab CRCs
    // commenting out the below two lines because we're not using this crc
    // see above for description.
    //
    // however, i'd still like the code to be here in case we need it in the future.
    // uint8_t baby_crc            = b[14];
    // uint8_t baby_calculated_crc = crc8(&b[8], 6, 1, 1);

    // total CRC-16
    // this CRC represents the entire packet sent by the modem in the remote
    uint16_t total_crc            = (b[15] << 8) | (b[16]);
    uint16_t calculated_total_crt = crc16(&b[7], 8, 0x8005, 0xFFFF);

    if (total_crc != calculated_total_crt) {
        // parity fail.
        return DECODE_FAIL_MIC;
    }

    /* clang-format off */
    data = data_make(
            "model",                  "",                         DATA_STRING, "SRSmith-SRS2CTX",
            "id",                     "Id",                       DATA_STRING, pin_string, // technically peoples pins should be different on each remote
            "totallength",            "Total Length",             DATA_INT, totallength,
            "packetsize",             "Packet Size",              DATA_INT, packetsize,
            "extracted_pin",          "Extracted PIN",            DATA_STRING, pin_string,
            "button_pressed_id",      "Pushed Button ID",         DATA_FORMAT, "%02x", DATA_INT, button_id,
            "button_pressed_string",  "Pushed Button String",     DATA_STRING, button_string,
            "total_crc",              "Total CRC",                DATA_FORMAT, "%04x", DATA_INT, total_crc,
            "unknown",                "Unknown",                  DATA_FORMAT, "%08x", DATA_INT, unknown_field,
            "mic",                    "Integrity",                DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "mic",
        "id",
        "totallength",
        "packetsize",
        "crc",
        "calculated_crc",
        "button_pressed_id",
        "extracted_pin",
        "unknown",
        NULL,
};

r_device srsmith_pool_srs_2c_tx = {
        .name        = "SRSmith Pool Light Remote Control SRS-2C-TX (-f 915000000)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 4096,
        .decode_fn   = &srsmith_pool_srs_2c_tx_callback,
        .fields      = output_fields,
};
