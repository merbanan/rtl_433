/* Template decoder
 *
 * Copyright © 2016 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
 *
 */
#define MYDEVICE_BITLEN        68
#define MYDEVICE_STARTBYTE    0xAA
#define MYDEVICE_MSG_TYPE    0x10
#define MYDEVICE_CRC_POLY    0x80
#define MYDEVICE_CRC_INIT    0x00


static int template_callback(bitbuffer_t *bitbuffer) {
    char time_str[LOCAL_TIME_BUFLEN];
    uint8_t *bb;
    uint16_t brow, row_nbytes;
    uint16_t sensor_id = 0;
    uint8_t msg_type, r_crc, c_crc;
    int16_t value;
    data_t *data;
    int valid = 0;


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

    local_time_str(0, time_str);

    /*
     * bit buffer will contain multiple rows, many of them empty.
     * Typically a complete message will be contained in a single
     * row if long and reset limits are set correctly.
     * May contain multiple message repeats.
     * Message might not appear in row 0, if protocol uses
     * start/preamble periods of different lengths.
     */

    for (brow = 0; brow < bitbuffer->num_rows; ++brow) {
    bb = bitbuffer->bb[brow];

    /*
     * Validate message and reject invalid messages as
     * early as possible before attempting to parse data..
     *
     * Check "message envelope"
     * - valid message length
     * - valid preamble/device type/fixed bits if any
     * - Data integrity checks (CRC/Checksum/Parity)
     */

    if (bitbuffer->bits_per_row[brow] != 68)
        continue;

    /*
     * number of bytes in row.
     *
     * Number of decoded bits may not be a multiple of 8.
     * bitbuffer row will have enough bytes to contain
     * all bytes, so round up.
     */
    row_nbytes = (bitbuffer->bits_per_row[brow] + 7)/8;


    /*
     * Reject rows that don't start with the correct start byte
     * Example message should start with 0xAA
     */
    if (bb[0] != MYDEVICE_STARTBYTE)
        continue;

    /*
     * Check message integrity (CRC/Checksum/parity)
     *
     * Example device uses CRC-8
     */
    r_crc = bb[row_nbytes - 1];
    c_crc = crc8(bb, row_nbytes - 1, MYDEVICE_CRC_POLY, MYDEVICE_CRC_INIT);
    if (r_crc != c_crc) {
        // example debugging output
        if (debug_output >= 1)
        fprintf(stderr, "%s new_tamplate bad CRC: calculated %02x, received %02x\n",
            time_str, c_crc, r_crc);

        // reject row
        continue;
    }

    /*
     * Now that message "envelope" has been validated,
     * start parsing data.
     */

    msg_type = bb[1];
    sensor_id = bb[2] << 8 | bb[3];
    value = bb[4] << 8 | bb[5];

    if (msg_type != MYDEVICE_MSG_TYPE) {
        /*
         * received an unexpected message type
         * could be a bad message or a new message not
         * previously seen.  Optionally log debug putput.
         */
        continue;
    }

    data = data_make("time", "", DATA_STRING, time_str,
        "model", "", DATA_STRING, "New Template",
        "id", "", DATA_INT, sensor_id,
        "data","", DATA_INT, value,
        NULL);

    data_acquired_handler(data);

    valid++;
    }

    // Return 1 if message successfully decoded
    if (valid)
    return 1;

    return 0;
}

/*
 * List of fields to output when using CSV
 *
 * Used to determine what fields will be output in what
 * order for this devince when using -F csv.
 *
 */
static char *csv_output_fields[] = {
    "time",
    "model",
    "id",
    "data",
    NULL
};

/*
 * r_device - registers device/callback. see rtl_433_devices.h
 *
 * Timings:
 *
 * short, long, nad reset - specify pulse/period timings
 *     based on number of samples at 250 Khz samples/second.
 *     These timings will determine if the received pulses
 *     match, so your callback will fire after demodulation.
 *
 * for readabiliy, specify timings based on 1 Mhz samples
 *     but a divide by 4 in the definition.
 *
 *
 * Demodular:
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
    .short_limit   = ((56+33)/2)*4,
    .long_limit    = (56+33)*4,
    .reset_limit   = (56+33)*2*4,
    .json_callback = &template_callback,
    .disabled      = 1,
    .demod_arg     = 0,
    .fields        = csv_output_fields,
};
