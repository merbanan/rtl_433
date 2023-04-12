/** @file
    SRSmith Pool Light Remote Control, Model #SRS-2C-TX.

    Copyright (C) 2022 gcohen55

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
SRSmith Pool Light Remote Control, Model #SRS-2C-TX.

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

Capture raw:

    -f 915M -X n=SRSmith,m=FSK_PCM,s=100,l=100,r=4096,preamble=d391d391
*/

// size byte + 7 byte message + two byte crc == 10 bytes
#define TOTAL_PACKET_SIZE_BYTES 10
#define BUTTON_ID_ONE           0x0d
#define BUTTON_ID_TWO           0x1f
#define BUTTON_ID_S             0x07
#define BUTTON_ID_M             0x0b

static int srsmith_pool_srs_2c_tx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // part of preamble + sync word
    uint8_t const preamble[] = {0xaa, 0xd3, 0x91, 0xd3, 0x91};
    int const preamble_length = sizeof(preamble) * 8;

    if (bitbuffer->num_rows != 1)
        return DECODE_ABORT_EARLY;

    // minimum: TOTAL_PACKET_SIZE_BYTES * 8 + sync word length (4)*8 + preamble byte (1*8) == 120
    // maximum: TOTAL_PACKET_SIZE_BYTES * 8 + sync word length (4)*8 + preamble bytes (4*8) == 144
    if (bitbuffer->bits_per_row[0] < 120 || bitbuffer->bits_per_row[0] > 144)
        return DECODE_ABORT_LENGTH;

    // next line does the search for the preamble+sync bits, returns the bit position where the preamble+sync bits START
    // so we shift that by the number of the preamble+sync bits
    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0, preamble, preamble_length) + preamble_length;
    if (start_pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY; // preamble/sync missing
    }

    // bytes are there, length is right, let's extract into b
    uint8_t b[TOTAL_PACKET_SIZE_BYTES];
    // now we're extracting the bytes -- we know what the total size /should/ be
    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, b, TOTAL_PACKET_SIZE_BYTES * 8);
    int total_length      = bitbuffer->bits_per_row[0];
    int sub_packet_length = b[0];

    // sub-packet (packet within packet that has commands and baby parity) actually starts at b[1]
    uint32_t unknown_field = ((uint32_t)b[1] << 24) | (b[2] << 16) | (b[3] << 8) | (b[4]); // not sure what these four bytes are.
                                                                                           //
    // get xmitted pin (byte 12, inverted, reversed, bottom nibble)
    uint8_t pin_container          = b[5];
    uint8_t inverted_pin_container = ~pin_container;                   // invert the pin
    uint8_t reversed_pin           = reverse8(inverted_pin_container); // reverse the bits

    // convert just the first four bits to string that contains 4 bits that the pin is
    char pin_string[5] = {0};
    snprintf(pin_string, sizeof(pin_string), "%d%d%d%d",
            (reversed_pin & 0x80 ? 1 : 0),
            (reversed_pin & 0x40 ? 1 : 0),
            (reversed_pin & 0x20 ? 1 : 0),
            (reversed_pin & 0x10 ? 1 : 0));

    // button that was pressed
    uint8_t button_id = b[6];

    // get label for the button that was pressed
    char const *button_string;

    switch (button_id) {
    case BUTTON_ID_ONE:
        button_string = "On/Off Channel 1";
        break;
    case BUTTON_ID_TWO:
        button_string = "On/Off Channel 2";
        break;
    case BUTTON_ID_S:
        button_string = "Color Sync";
        break;
    case BUTTON_ID_M:
        button_string = "ON/OFF Control - M";
        break;
    default:
        button_string = "Unknown";
        break;
    }

    // grab CRCs/parity
    // the "sub packet parity" is the parity for the sub-packet within the modem packet (e.g., between the size byte and the start of the crc-16)
    // see above for description.
    uint8_t sub_packet_parity            = b[7];
    uint8_t calculated_sub_packet_parity = crc8(&b[1], 6, 1, 1);

    // total CRC-16
    // this CRC represents the entire packet sent by the modem in the remote
    uint16_t total_crc            = (b[8] << 8) | (b[9]);
    uint16_t calculated_total_crc = crc16(&b[0], 8, 0x8005, 0xFFFF);
    decoder_logf(decoder, 1, __func__,
            "total_length: %d, sub_packet_length: %d, sub_packet_parity: %hhx, calculated_sub_packet_parity: %hhx, total_crc: %04x, calculated_total_crc: %04x, button_id: %hhx, button_string: %s, pin_string: %s",
            total_length, sub_packet_length, sub_packet_parity, calculated_sub_packet_parity, total_crc, calculated_total_crc, button_id, button_string, pin_string);

    if (total_crc != calculated_total_crc) {
        // parity fail.
        return DECODE_FAIL_MIC;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",                  "",                         DATA_STRING, "SRSmith-SRS2CTX",
            "id",                     "Id",                       DATA_INT, reversed_pin, // technically peoples pins should be different on each remote
            "button_press",           "Pushed Button ID",         DATA_FORMAT, "%02x", DATA_INT, button_id,
            "button_press_name",      "Pushed Button String",     DATA_STRING, button_string,
            "unknown",                "Unknown",                  DATA_FORMAT, "%08x", DATA_INT, unknown_field,
            "mic",                    "Integrity",                DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "mic",
        "id",
        "button_press",
        "button_press_name",
        "unknown",
        NULL,
};

r_device const srsmith_pool_srs_2c_tx = {
        .name        = "SRSmith Pool Light Remote Control SRS-2C-TX (-f 915M)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 4096,
        .decode_fn   = &srsmith_pool_srs_2c_tx_decode,
        .fields      = output_fields,
};
