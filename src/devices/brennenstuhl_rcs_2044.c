/*
 * Brennenstuhl RCS 2044 remote control on 433.92MHz
 * likely x1527
 *
 * Copyright (C) 2015 Paul Ortyl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

/*
 * Receiver for the "RCS 2044 N Comfort Wireless Controller Set" sold under
 * the "Brennenstuhl" brand.
 *
 * The protocol is also implemented for raspi controlled transmitter on 433.92 MHz:
 * https://github.com/xkonni/raspberry-remote
 */

#include "decoder.h"

static int brennenstuhl_rcs_2044_process_row(r_device *decoder, bitbuffer_t const *bitbuffer, int row)
{
    uint8_t const *b = bitbuffer->bb[row];
    int const length = bitbuffer->bits_per_row[row];
    data_t *data;

    /* Test bit pattern for every second bit being 1 */
    if ( 25 != length
        || (b[0]&0xaa) != 0xaa
        || (b[1]&0xaa) != 0xaa
        || (b[2]&0xaa) != 0xaa
        || (b[3]       != 0x80) )
        return 0; /* something is wrong, exit now */

    /* Only odd bits contain information, even bits are set to 1
     * First 5 odd bits contain system code (the dip switch on the remote),
     * following 5 odd bits code button row pressed on the remote,
     * following 2 odd bits code button column pressed on the remote.
     *
     * One can press many buttons at a time and the corresponding code will be sent.
     * In the code below only use of a single button at a time is reported,
     * all other messages are discarded as invalid.
     */

    /* extract bits for system code */
    int system_code =
              (b[0] & 0x40) >> 2
            | (b[0] & 0x10) >> 1
            | (b[0] & 0x04)
            | (b[0] & 0x01) << 1
            | (b[1] & 0x40) >> 6;

    /* extract bits for pressed key row */
    int control_key =
              (b[1] & 0x10)
            | (b[1] & 0x04) << 1
            | (b[1] & 0x01) << 2
            | (b[2] & 0x40) >> 5
            | (b[2] & 0x10) >> 4;

    /* Test if the message is valid. It is possible to press multiple keys on the
     * remote at the same time.    As all keys are transmitted orthogonally, this
     * information can be transmitted.    This use case is not the usual use case
     * so we can use it for validation of the message:
     * ONLY ONE KEY AT A TIME IS ACCEPTED.
     */
    char *key = NULL;
    if (control_key == 0x10)
        key = "A";
    else if (control_key == 0x08)
        key = "B";
    else if (control_key == 0x04)
        key = "C";
    else if (control_key == 0x02)
        key = "D";
    else if (control_key == 0x01)
        key = "E"; /* (does not exist on the remote, but can be set and is accepted by receiver) */
    else return 0;
    /* None of the keys has been pressed and we still received a message.
     * Skip it. It happens sometimes as the last code repetition
     */

    /* extract on/off bits (first or second key column on the remote) */
    int on_off = (b[2] & 0x04) >> 1 | (b[2] & 0x01);

    if (on_off != 0x02 && on_off != 0x01)
        return 0; /* Pressing simultaneously ON and OFF key is not useful either */

    data = data_make(
            "model",    "Model",    DATA_STRING, _X("Brennenstuhl-RCS2044","Brennenstuhl RCS 2044"),
            "id",       "id",       DATA_INT, system_code,
            "key",      "key",      DATA_STRING, key,
            "state",    "state",    DATA_STRING, (on_off == 0x02 ? "ON" : "OFF"),
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static int brennenstuhl_rcs_2044_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int counter = 0;
    for (int row = 0; row < bitbuffer->num_rows; row++)
        counter += brennenstuhl_rcs_2044_process_row(decoder, bitbuffer, row);
    return counter;
}

static char *output_fields[] = {
    "model",
    "type",
    "state",
    NULL
};

r_device brennenstuhl_rcs_2044 = {
    .name          = "Brennenstuhl RCS 2044",
    .modulation    = OOK_PULSE_PWM,
    .short_width   = 320,
    .long_width    = 968,
    .gap_limit     = 1500,
    .reset_limit   = 4000,
    .decode_fn     = &brennenstuhl_rcs_2044_callback,
    .disabled      = 1,
    .fields        = output_fields,
};
