/** @file
    Template decoder for DEVICE, tested with BRAND, BRAND.

    Copyright (C) 2016 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/*
    Use this as a starting point for a new decoder.

    Keep the Doxygen (slash-star-star) comment above to document the file and copyright.

    Keep the Doxygen (slash-star-star) comment below to describe the decoder.
    See http://www.doxygen.nl/manual/markdown.html for the formatting options.

    Remove all other multiline (slash-star) comments.
    Use single-line (slash-slash) comments to annontate important lines if needed.

    To use this:
    - Copy this template to a new file
    - Change at least `new_template` in the source
    - Add to include/rtl_433_devices.h
    - Run ./maintainer_update.py (needs a clean git stage or commit)

    Note that for simple devices doorbell/PIR/remotes a flex conf (see conf dir) is preferred.
*/

#include "decoder.h"

/**
(this is a markdown formatted section to describe the decoder)
(the first line here should match the first documentation line of the file, e.g.)
Template decoder for DEVICE, tested with BRAND, BRAND.

(describe the modulation, timing, and transmission, e.g.)
The device uses PPM encoding,
- 0 is encoded as 40 us pulse and 132 us gap,
- 1 is encoded as 40 us pulse and 224 us gap.
The device sends a transmission every 63 seconds.
A transmission starts with a preamble of 0xAA,
there a 5 repeated packets, each with a 1200 us gap.

(describe the data and payload, e.g.)
Data layout:
    (preferably use one character per bit)
    FFFFFFFF PPPPPPPP PPPPPPPP IIIIIIII IIIIIIII IIIIIIII TTTTTTTT TTTTTTTT CCCCCCCC
    (otherwise use one character per nibble if this fits well)
    FF PP PP II II II TT TT CC

- F: 8 bit flags, (0x40 is battery_low)
- P: 16-bit little-endian Pressure
- I: 24-bit little-endian id
- T: 16-bit little-endian Unknown, likely Temperature
- C: 8 bit Checksum, CRC-8 truncated poly 0x07 init 0x00
*/
static int new_template_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    /*
     * Early debugging aid to see demodulated bits in buffer and
     * to determine if your limit settings are matched and firing
     * this decode callback.
     *
     * 1. Enable with -vvv (debug decoders)
     * 2. Delete this block when your decoder is working
     */
    //    decoder_log_bitbuffer(decoder, 2, __func__, bitbuffer, "");

    /*
     * If you expect the bits flipped with respect to the demod
     * invert the whole bit buffer.
     */

    bitbuffer_invert(bitbuffer);

    /*
     * The bit buffer will contain multiple rows.
     * Typically a complete message will be contained in a single
     * row if long and reset limits are set correctly.
     * May contain multiple message repeats.
     * Message might not appear in row 0, if protocol uses
     * start/preamble periods of different lengths.
     */

    /*
     * Either, if you expect just a single packet
     * loop over all rows and collect or output data:
     */

    uint8_t *b; // bits of a row
    int r;
    for (r = 0; r < bitbuffer->num_rows; ++r) {
        b = bitbuffer->bb[r];

        /*
         * Validate message and reject invalid messages as
         * early as possible before attempting to parse data.
         *
         * Check "message envelope"
         * - valid message length (use a minimum length to account
         *   for stray bits appended or prepended by the demod)
         * - valid preamble/device type/fixed bits if any
         * - Data integrity checks (CRC/Checksum/Parity)
         */

        // Message is expected to be 68 bits long
        if (bitbuffer->bits_per_row[r] < 68) {
            continue; // not enough bits
        }

        if (b[0] != 0x42) {
            continue; // magic header not found
        }

        /*
         * ... see below and replace `return 0;` with `continue;`
         */
    }

    /*
     * Or, if you expect repeated packets
     * find a suitable row:
     */

    // The message is repeated as 5 packets, require at least 3 repeated packets of 68 bits.
    r = bitbuffer_find_repeated_row(bitbuffer, 3, 68);
    if (r < 0 || bitbuffer->bits_per_row[r] > 68 + 16) {
        return DECODE_ABORT_LENGTH;
    }

    b = bitbuffer->bb[r];

    /*
     * Either reject rows that don't start with the correct start byte:
     * Example message should start with 0xAA
     */
    if (b[0] != 0xaa) {
        return DECODE_ABORT_EARLY; // Messages start of 0xAA not found
    }

    /*
     * Or (preferred) search for the message preamble:
     * See bitbuffer_search()
     */

    /*
     * Several tools are available to reverse engineer a message integrity
     * check:
     *
     * - reveng for CRC: http://reveng.sourceforge.net/
     *   - Guide: https://hackaday.com/2019/06/27/reverse-engineering-cyclic-redundancy-codes/
     * - revdgst: https://github.com/triq-org/revdgst/
     * - trial and error, e.g. via online calculators:
     *   - https://www.scadacore.com/tools/programming-calculators/online-checksum-calculator/
     */

    /*
     * Check message integrity (Parity example)
     *
     */
    // parity check: odd parity on bits [0 .. 67]
    // i.e. 8 bytes and a nibble.
    int parity;
    parity = b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4] ^ b[5] ^ b[6] ^ b[7]; // parity as byte
    parity = (parity >> 4) ^ (parity & 0xF);                        // fold to nibble
    parity ^= b[8] >> 4;                                            // add remaining nibble
    parity = (parity >> 2) ^ (parity & 0x3);                        // fold to 2 bits
    parity = (parity >> 1) ^ (parity & 0x1);                        // fold to 1 bit

    if (!parity) {
        // Enable with -vv (verbose decoders)
        decoder_log(decoder, 1, __func__, "parity check failed");
        return DECODE_FAIL_MIC;
    }

    /*
     * Check message integrity (Checksum example)
     */
    if (((b[0] + b[1] + b[2] + b[3] - b[4]) & 0xFF) != 0) {
        // Enable with -vv (verbose decoders)
        decoder_log(decoder, 1, __func__, "checksum error");
        return DECODE_FAIL_MIC;
    }

    /*
     * Check message integrity (CRC example)
     *
     * Example device uses CRC-8
     */
    // There are 6 data bytes and then a CRC8 byte
    int chk = crc8(b, 7, 0x07, 0x00);
    if (chk != 0) {
        // Enable with -vv (verbose decoders)
        decoder_log(decoder, 1, __func__, "bad CRC");

        // reject row
        return DECODE_FAIL_MIC;
    }

    /*
     * Now that message "envelope" has been validated,
     * start parsing data.
     */
    int msg_type  = b[1];
    int sensor_id = b[2] << 8 | b[3];
    int value     = b[4] << 8 | b[5];

    // A message type byte of 0x10 is expected
    if (msg_type != 0x10) {
        /*
         * received an unexpected message type
         * could be a bad message or a new message not
         * previously seen.  Optionally log debug output.
         */
        return DECODE_FAIL_OTHER;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model", "", DATA_STRING, "New-Template",
            "id",    "", DATA_INT,    sensor_id,
            "data",  "", DATA_INT,    value,
            "mic",   "", DATA_STRING, "CHECKSUM", // CRC, CHECKSUM, or PARITY
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

    // Return 1 if message successfully decoded
    return 1;
}

/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */
static char const *const output_fields[] = {
        "model",
        "id",
        "data",
        "mic", // remove if not applicable
        NULL,
};

/*
 * r_device - registers device/callback. see rtl_433_devices.h
 *
 * Timings:
 *
 * short, long, and reset - specify pulse/period timings in [us].
 *     These timings will determine if the received pulses
 *     match, so your callback will fire after demodulation.
 *
 * Modulation:
 *
 * The function used to turn the received signal into bits.
 * See:
 * - pulse_slicer.h for descriptions
 * - r_device.h for the list of defined names
 *
 * This device is disabled and hidden, it can not be enabled.
 *
 * To enable your device, append it to the list in include/rtl_433_devices.h
 * and sort it into src/CMakeLists.txt or run ./maintainer_update.py
 *
 */
r_device const new_template = {
        .name        = "Template decoder",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 132,  // short gap is 132 us
        .long_width  = 224,  // long gap is 224 us
        .gap_limit   = 300,  // some distance above long
        .reset_limit = 1000, // a bit longer than packet gap
        .decode_fn   = &new_template_decode,
        .disabled    = 3, // disabled and hidden, use 0 if there is a MIC, 1 otherwise
        .fields      = output_fields,
};
