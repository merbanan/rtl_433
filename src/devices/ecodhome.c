/** @file
    Decoder for EcoDHOME Smart Socket and MCEE Solar monitor.

    Copyright (C) 2020 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for EcoDHOME Smart Socket and MCEE Solar monitor.

(the Smart Switch should be the same as the Smart Socket.)

Smart Socket receives and implements the switching on/off instruction remotely from the Controller.
The Transmitters with sensor clamps collect home energy consumption data for the MCEE Solar monitor.

see https://github.com/merbanan/rtl_433/issues/1525

The transmission is FSK PCM with 250 us bit width.

## PV Transmitter (P/N 01333-5847-00)

Example data:

    {144}aaaaaa 2dd4 8c74 d4b9 3eb3 223844 51 550000
    {144}aaaaaa 2dd4 8c74 d4b9 3eb3 c53344 ef 550000
    {144}aaaaaa 2dd4 8c76 d4b9 71b3 863363 04 550000 (every 71 seconds)

Other device:

    {144}aaaaaa 2dd4 8c74 12d6  Type: 3eb3 bc3544
    {144}aaaaaa 2dd4 8c76 12d6  Type: 71b3 333363 (also 863363)

- 3eb3 messages are a power reading of LL HH 0x44, LL and HH start at 0x33 (=0) and wrap up to 0x32 (=255)
- 71b3 messages (arrive every 71 seconds)
- 71b3 863363 04 550000 which might be some kind of status then and not a reading.

The checksum is: add all bytes after the sync word plus 0x35 (mod 0xff).

## Smart Socket (P/N 01333-5840-00)

Example data:

    {155}2ad455555516ea2918ae353b802b2d3f8029a12
    {154}55a8 aaaaaa 2dd4 5231 5c6a 7700 565a 7f00 53 42 4

Data Seen:

    52315c6a 7700 565a 007f00
    52315c6a 7700 565a 007e00
    52315c6a 7700 565a 007d00
    46315c6a 7700 414b 000000
    52315c6a 7700 565a 008000
    46315c6a 7700 5053 000000
    52315c6a 7700 414b 000000
    52315c6a 7700 565a 008100
    52315c6a 7700 565a 008200
    52315c6a 7700 565a 008300
    52315c6a 7700 414b 003209
    52315c6a 7700 414b 003d03
    52315c6a 7700 565a 007c00
    52315c6a 7700 565a 007b00
    52315c6a 7700 565a 007a00

Removing the first 1 or 2 bits gives a prefix of 55a8aaaaaa2dd4, the leading bits are likely warm-up or garbage.

The next bytes of 5231 5c6a 7700 are likely a serial number (id).

Then we have messages with 414b or 565a or 5053 which likely is a message type.
On 414b the two byte (little endian) power value follows. For the other types it is unknown, maybe kWh or state.
Lastly there is a fixed 53 (status? stop?) and a checksum byte.

Interesting to note that 414b, 565a, and 53 are "AK", "VZ", and "S" which might not be a coincidence.

The checksum is: add all bytes after the sync word (mod 0xff).
*/

static int ecodhome_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0x2d, 0xd4};

    data_t *data;
    uint8_t msg[13];

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] < 128) {
        decoder_logf(decoder, 2, __func__, "to few bits (%u)", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH; // unrecognized
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);
    start_pos += sizeof(preamble_pattern) * 8;

    if (start_pos >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "preamble not found");
        return DECODE_ABORT_EARLY; // no preamble found
    }
    //if (start_pos + sizeof (msg) * 8 >= bitbuffer->bits_per_row[0]) {
    if (start_pos + 12 * 8 >= bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 2, __func__, "message too short (%u)", bitbuffer->bits_per_row[0] - start_pos);
        return DECODE_ABORT_LENGTH; // message too short
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof(msg) * 8);
    decoder_log_bitrow(decoder, 2, __func__, msg, sizeof(msg) * 8, "MSG");

    uint32_t id   = ((uint32_t)msg[0] << 24) | (msg[1] << 16) | (msg[2] << 8) | (msg[3]);
    int m_type    = (msg[4] << 8) | (msg[5]);
    int m_subtype = (msg[6] << 8) | (msg[7]); // only Smart Socket

    if (m_type == 0x7700) {
        int sum = add_bytes(msg, 11); // socket
        if ((sum & 0xff) != msg[11]) {
            decoder_logf(decoder, 2, __func__, "checksum fail %02x vs %02x", sum, msg[9]);
            return DECODE_FAIL_MIC;
        }
        if (msg[10] != 0x53) {
            decoder_logf(decoder, 2, __func__, "wrong stop byte %02x", msg[10]);
            return DECODE_FAIL_SANITY;
        }
        int raw     = (msg[8] << 8) | (msg[9]);
        int power_w = (msg[9] << 8) | (msg[8]);

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "EcoDHOME-SmartSocket",
                "id",               "",                 DATA_FORMAT, "%08x",   DATA_INT, id,
                "message_type",     "Message Type",     DATA_FORMAT, "%04x",   DATA_INT, m_type,
                "message_subtype",  "Message Subtype",  DATA_FORMAT, "%04x",   DATA_INT, m_subtype,
                "power_W",          "Power",            DATA_COND, m_subtype == 0x414b, DATA_FORMAT, "%.1f W",   DATA_DOUBLE, (double)power_w,
                "raw",              "Raw data",         DATA_FORMAT, "%06x",   DATA_INT, raw,
                "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */
    }
    else {
        int sum = add_bytes(msg, 9) + 0x35; // transmitter
        if ((sum & 0xff) != msg[9]) {
            decoder_logf(decoder, 2, __func__, "checksum fail %02x vs %02x", sum, msg[9]);
            return DECODE_FAIL_MIC;
        }
        if (msg[10] != 0x55) {
            decoder_logf(decoder, 2, __func__, "wrong stop byte %02x", msg[10]);
            return DECODE_FAIL_SANITY;
        }
        if (msg[11] != 0x00) {
            decoder_logf(decoder, 2, __func__, "wrong poststop byte %02x", msg[11]);
            return DECODE_FAIL_SANITY;
        }
        int raw     = (msg[6] << 16) | (msg[7] << 8) | (msg[8]);
        int power_w = ((uint8_t)(msg[7] - 0x33) << 8) | (uint8_t)(msg[6] - 0x33);

        /* clang-format off */
        data = data_make(
                "model",            "",                 DATA_STRING, "EcoDHOME-Transmitter",
                "id",               "",                 DATA_FORMAT, "%08x",   DATA_INT, id,
                "message_type",     "Message Type",     DATA_FORMAT, "%04x",   DATA_INT, m_type,
                "power_W",          "Power",            DATA_COND, m_type == 0x3eb3, DATA_FORMAT, "%.1f W",   DATA_DOUBLE, (double)power_w,
                "raw",              "Raw data",         DATA_FORMAT, "%06x",   DATA_INT, raw,
                "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */
    }

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "message_type",
        "message_subtype",
        "power_W",
        "raw",
        "mic",
        NULL,
};

r_device const ecodhome = {
        .name        = "EcoDHOME Smart Socket and MCEE Solar monitor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 250,
        .long_width  = 250,
        .reset_limit = 6000,
        .decode_fn   = &ecodhome_decode,
        .fields      = output_fields,
};
