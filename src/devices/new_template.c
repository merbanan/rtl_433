/* Template decoder for DEVICE, tested with BRAND, BRAND.
 *
 * Copyright (C) 2016 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * (describe the modulation, timing, and transmission, e.g.)
 * The device uses PPM encoding,
 * 0 is encoded as 40 us pulse and 132 us gap,
 * 1 is encoded as 40 us pulse and 224 us gap.
 * The device sends a transmission every 63 seconds.
 * A transmission starts with a preamble of 0xAA,
 * there a 5 repeated packets, each with a 1200 us gap.
 *
 * (describe the data and payload, e.g.)
 * Packet nibbles:  FF PP PP II II II TT TT CC
 * F = flags, (0x40 is battery_low)
 * P = Pressure, 16-bit little-endian
 * I = id, 24-bit little-endian
 * T = Unknown, likely Temperature, 16-bit little-endian
 * C = Checksum, CRC-8 truncated poly 0x07 init 0x00
 *
 */

/* Use this as a starting point for a new decoder. */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

/*
 * Hypothetical template device
 *
 * Message is 68 bits long
 * Messages start with 0xAA
 * The message is repeated as 5 packets,
 * require at least 3 repeated packets.
 *
 */
#define MYDEVICE_BITLEN      68
#define MYDEVICE_STARTBYTE   0xAA
#define MYDEVICE_MINREPEATS  3
#define MYDEVICE_MSG_TYPE    0x10
#define MYDEVICE_CRC_POLY    0x07
#define MYDEVICE_CRC_INIT    0x00

static int template_callback(bitbuffer_t *bitbuffer)
{
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    int r; // a row index
    uint8_t *b; // bits of a row
    int parity;
    uint8_t r_crc, c_crc;
    uint16_t sensor_id;
    uint8_t msg_type;
    int16_t value;

    /*
     * Early debugging aid to see demodulated bits in buffer and
     * to determine if your limit settings are matched and firing
     * this callback.
     *
     * 1. Enable with -D -D (debug level of 2)
     * 2. Delete this block when your decoder is working
     */
    //    if (debug_output > 1) {
    //        fprintf(stderr,"new_tmplate callback:\n");
    //        bitbuffer_print(bitbuffer);
    //    }

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

        if (bitbuffer->bits_per_row[r] < MYDEVICE_BITLEN) {
            continue;
        }

        /*
         * ... see below and replace `return 0;` with `continue;`
         */
    }

    /*
     * Or, if you expect repeated packets
     * find a suitable row:
     */

    r = bitbuffer_find_repeated_row(bitbuffer, MYDEVICE_MINREPEATS, MYDEVICE_BITLEN);
    if (r < 0 || bitbuffer->bits_per_row[r] > MYDEVICE_BITLEN + 16) {
        return 0;
    }

    b = bitbuffer->bb[r];

    /*
     * Either reject rows that don't start with the correct start byte:
     * Example message should start with 0xAA
     */
    if (b[0] != MYDEVICE_STARTBYTE) {
        return 0;
    }

    /*
     * Or (preferred) search for the message preamble:
     * See bitbuffer_search()
     */

    /*
     * Check message integrity (Parity example)
     */
    // parity check: odd parity on bits [0 .. 67]
    // i.e. 8 bytes and a nibble.
    parity = b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4] ^ b[5] ^ b[6] ^ b[7]; // parity as byte
    parity = (parity >> 4) ^ (parity & 0xF); // fold to nibble
    parity ^= b[8] >> 4; // add remaining nibble
    parity = (parity >> 2) ^ (parity & 0x3); // fold to 2 bits
    parity = (parity >> 1) ^ (parity & 0x1); // fold to 1 bit

    if (!parity) {
        if (debug_output) {
            fprintf(stderr, "new_template parity check failed\n");
        }
        return 0;
    }

    /*
     * Check message integrity (Checksum example)
     */
    if (((b[0] + b[1] + b[2] + b[3] - b[4]) & 0xFF) != 0) {
        if (debug_output) {
            fprintf(stderr, "new_template checksum error\n");
        }
        return 0;
    }

    /*
     * Check message integrity (CRC example)
     *
     * Example device uses CRC-8
     */
    r_crc = b[7];
    c_crc = crc8(b, MYDEVICE_BITLEN / 8, MYDEVICE_CRC_POLY, MYDEVICE_CRC_INIT);
    if (r_crc != c_crc) {
        // example debugging output
        if (debug_output) {
            fprintf(stderr, "new_template bad CRC: calculated %02x, received %02x\n",
                    c_crc, r_crc);
        }

        // reject row
        return 0;
    }

    /*
     * Now that message "envelope" has been validated,
     * start parsing data.
     */
    msg_type = b[1];
    sensor_id = b[2] << 8 | b[3];
    value = b[4] << 8 | b[5];

    if (msg_type != MYDEVICE_MSG_TYPE) {
        /*
         * received an unexpected message type
         * could be a bad message or a new message not
         * previously seen.  Optionally log debug output.
         */
        return 0;
    }

    local_time_str(0, time_str);

    data = data_make(
            "time",  "", DATA_STRING, time_str,
            "model", "", DATA_STRING, "New Template",
            "id",    "", DATA_INT,    sensor_id,
            "data",  "", DATA_INT,    value,
            "mic",   "", DATA_STRING, "CHECKSUM", // CRC, CHECKSUM, or PARITY
            NULL);

    data_acquired_handler(data);

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
static char *output_fields[] = {
    "time",
    "model",
    "id",
    "data",
    "mic", // remove if not applicable
    NULL
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
 * - pulse_demod.h for descriptions
 * - rtL_433.h for the list of defined names
 *
 * This device is disabled by default. Enable it with -R 61 on the commandline
 */
r_device template = {
    .name          = "Template decoder",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = (224 + 132) / 2, // short gap is 132 us, long gap is 224 us
    .long_limit    = 224 + 132,
    .reset_limit   = (224 + 132) * 2,
    .json_callback = &template_callback,
    .disabled      = 1,
    .demod_arg     = 0,
    .fields        = output_fields,
};
