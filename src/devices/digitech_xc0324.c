/** @file
    Decoder for Digitech XC-0324 temperature sensor.

    Copyright (C) 2018 Geoff Lee

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn static int xc0324_callback(r_device *decoder, bitbuffer_t *bitbuffer)
Digitech XC-0324 device.

The encoding is pulse position modulation
(i.e. gap width contains the modulation information)
- pulse is about 400 us
- short gap is (approx) 520 us
- long gap is (approx) 1000 us

Deciphered using two transmitters.

A transmission package is 148 bits
(plus or minus one or two due to demodulation or transmission errors).

Each transmission contains 3 repeats of the 48 bit message,
with 2 zero bits separating each repetition.

A 48 bit message consists of:
- byte 0: preamble (for synchronisation), 0x5F
- byte 1: device id
- byte 2 and the first nibble of byte 3: encodes the temperature
    as a 12 bit integer,
    transmitted in least significant bit first order
    in tenths of degree Celsius
    offset from -40.0 degrees C (minimum temp spec of the device)
- byte 4: constant (in all my data) 0x80
    _maybe_ a battery status ???
- byte 5: a check byte (the XOR of bytes 0-4 inclusive)
    each bit is effectively a parity bit for correspondingly positioned bit
    in the real message

This decoder is associated with a tutorial entry in the
rtl_433 wiki describing the way the transmissions were deciphered.
See https://github.com/merbanan/rtl_433/wiki/digitech_xc0324.README.md

The tutorial is "by a newbie, for a newbie", ie intended to assist newcomers
who wish to learn how to decipher a new device, and develop a rtl_433 device
decoder from scratch for the first time.

To illustrate stages in the deciphering process, this decoder includes some
debug style trace messages that would normally be removed. Specifically,
running this decoder with debug level :
- `-vvv` simulates what might be seen early in the deciphering process, when
    only the modulation scheme and parameters have been discovered,
- `-vv` simulates what might be seen once the synchronisation/preamble and
    message length has been uncovered, and it is time to start work on
    deciphering individual fields in the message,
    with no debug flags set provides the final (production stage) results,
    and
- `-vvvv` is a special "finished development" output.  It provides a file of
        reference values, to be included with the test data for future
        regression test purposes.
*/

#include "decoder.h"

#define XC0324_DEVICE_BITLEN      148
#define XC0324_MESSAGE_BITLEN     48
#define XC0324_MESSAGE_BYTELEN    (XC0324_MESSAGE_BITLEN + 7)/ 8
#define XC0324_DEVICE_STARTBYTE   0x5F
#define XC0324_DEVICE_MINREPEATS  3

static const uint8_t preamble_pattern[1] = {XC0324_DEVICE_STARTBYTE};

/// @param[out] data: returns the decoded information as a data_t*
static int decode_xc0324_message(r_device *decoder, bitbuffer_t *bitbuffer,
        unsigned row, uint16_t bitpos, const int latest_event, data_t **data)
{
    uint8_t b[XC0324_MESSAGE_BYTELEN];
    char id[4] = {0};
    double temperature;
    uint8_t flags;
    uint8_t chksum; // == 0x00 for a good message

    // Extract the message
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, XC0324_MESSAGE_BITLEN);

    // Examine the chksum and bail out now if not OK to save time
    // b[5] is a check byte, the XOR of bytes 0-4.
    // ie a checksum where the sum is "binary add no carry"
    // Effectively, each bit of b[5] is the parity of the bits in the
    // corresponding position of b[0] to b[4]
    // NB : b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4] ^ b[5] == 0x00 for a clean message
    chksum = xor_bytes(b, 6);
    if (chksum != 0x00) {
        if (decoder->verbose == 1) {
            // Output the "bad" message (only for message level deciphering!)
            decoder_output_bitrowf(decoder, b, XC0324_MESSAGE_BITLEN,
                    "chksum = 0x%02X not 0x00 <- XC0324:vv row %d bit %d",
                    chksum, row, bitpos);
        }
        return 0; // No message was able to be decoded
    }

    // Extract the id as hex string
    snprintf(id, 3, "%02X", b[1]);

    // Decode temperature (b[2]), plus 1st 4 bits b[3], LSB first order!
    // Tenths of degrees C, offset from the minimum possible (-40.0 degrees)
    uint16_t temp = ((uint16_t)(reverse8(b[3]) & 0x0f) << 8) | reverse8(b[2]);
    temperature   = (temp / 10.0) - 40.0;

    //Unknown byte, constant as 0x80 in all my data
    // ??maybe battery status??
    flags = b[4];

    // Create the data structure, ready for the decoder_output_data function.
    // Separate production output (decoder->verbose == 0)
    // from (simulated) deciphering stage output (decoder->verbose > 0)
    if (!decoder->verbose) { // production output
        *data = data_make(
                "model",            "Device Type",      DATA_STRING, _X("Digitech-XC0324","Digitech XC0324"),
                "id",               "ID",               DATA_STRING, id,
                "temperature_C",    "Temperature C",    DATA_FORMAT, "%.1f", DATA_DOUBLE, temperature,
                "flags",            "Constant ?",       DATA_INT,    flags,
                "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
                NULL);
    }

    // Output (simulated) message level deciphering information..
    if (decoder->verbose == 1) {
        decoder_output_bitrowf(decoder, b, XC0324_MESSAGE_BITLEN,
                "Temp was %4.1f <- XC0324:vv row %03d bit %03d",
                temperature, row, bitpos);
    }
    // Output "finished deciphering" reference values for future regression tests.
    if ((decoder->verbose == 3) & (latest_event == 0)) {
        //info from this first successful message is enough
        decoder_output_messagef(decoder,
                "XC0324:vvvv Reference -> Temperature %4.1f C; sensor id %s",
                temperature, id);
    }
    return 1; // Message successfully decoded
}

// List of fields to appear in the `-F csv` output
static char *output_fields[] = {
    "model",
    "id",
    "temperature_C",
    "flags",
    "mic",
    "message_num",
    NULL
};

static int xc0324_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r; // a row index
    uint16_t bitpos;
    int result;
    int events = 0;
    data_t *data = NULL;

    // Only for simulating initial package level deciphering / debug.
    if (decoder->verbose == 2) {
        // Verbosely output the bitbuffer
        decoder_output_bitbufferf(decoder, bitbuffer, "XC0324:vvv hex(/binary) version of bitbuffer");
        // And then output each row to csv, json or whatever was specified.
        for (r = 0; r < bitbuffer->num_rows; ++r) {
            decoder_output_bitrowf(decoder, bitbuffer->bb[r], bitbuffer->bits_per_row[r],
                    "XC0324:vvv row %03d", r);
        }
    }
    //A clean XC0324 transmission contains 3 repeats of a message in a single row.
    //But in case of transmission or demodulation glitches,
    //loop over all rows and check for salvageable messages.
    for (r = 0; r < bitbuffer->num_rows; ++r) {
        if (bitbuffer->bits_per_row[r] < XC0324_MESSAGE_BITLEN) {
            // bail out of this "too short" row early
            if (decoder->verbose == 1) {
                // Output the bad row, only for message level debug / deciphering.
                decoder_output_bitrowf(decoder, bitbuffer->bb[r], bitbuffer->bits_per_row[r],
                        "Bad message need %d bits got %d <- XC0324:vv row %d bit %d",
                        XC0324_MESSAGE_BITLEN, bitbuffer->bits_per_row[r], r, 0);
            }
            continue; // to the next row
        }
        // We have enough bits so search for a message preamble followed by
        // enough bits that it could be a complete message.
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, r, bitpos,
                        (const uint8_t *)&preamble_pattern, 8)) +
                        XC0324_MESSAGE_BITLEN <=
                bitbuffer->bits_per_row[r]) {
            events += result = decode_xc0324_message(decoder, bitbuffer,
                    r, bitpos, events, &data);
            // Keep production output (decoder->verbose == 0) separate from
            // (simulated) development stage output (decoder->verbose > 0)
            if (result & !decoder->verbose) { // Production output
                data_append(data,
                        "message_num", "Message repeat count", DATA_INT, events, NULL);
                decoder_output_data(decoder, data);
                return events; // in production, first successful decode is enough
            }
            bitpos += XC0324_MESSAGE_BITLEN;
        }
    }
    // (Only) for future regression tests.
    if ((decoder->verbose == 3) & (events == 0)) {
        decoder_output_messagef(decoder, "XC0324:vvvv Reference -> Bad transmission");
    }
    return events;
}

r_device digitech_xc0324 = {
    .name           = "Digitech XC-0324 temperature sensor",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 520, // = 130 * 4
    .long_width     = 1000, // = 250 * 4
    .reset_limit    = 3000,
    .decode_fn      = &xc0324_callback,
    .disabled       = 1, // stop debug output from spamming unsuspecting users
    .fields         = output_fields,
};
