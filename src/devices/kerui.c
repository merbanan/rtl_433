/** @file
    Kerui PIR / Contact Sensor.

    Copyright (C) 2016 Karl Lattimer

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Kerui PIR / Contact Sensor.

Such as
http://www.ebay.co.uk/sch/i.html?_from=R40&_trksid=p2050601.m570.l1313.TR0.TRC0.H0.Xkerui+pir.TRS0&_nkw=kerui+pir&_sacat=0

also tested with:
- KERUI D026 Window Door Magnet Sensor Detector (433MHz) https://fccid.io/2AGNGKR-D026
  events: open / close / tamper / battery low (below 5V of 12V battery)
- Water leak sensor WD51
- Mini Pir P831

Note: simple 24 bit fixed ID protocol (x1527 style) and should be handled by the flex decoder.
There is a leading sync bit with a wide gap which runs into the preceding packet, it's ignored as 25th data bit.

There are slight timing differences between the older sensors and new ones like Water leak sensor WD51 and Mini Pir P831.
Long: 860-1016 us, short: 304-560 us, older sync: 480 us, newer sync: 340 us,
*/

#include "decoder.h"

static int kerui_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int id;
    int cmd;
    char const *cmd_str;

    int r = bitbuffer_find_repeated_row(bitbuffer, 9, 25); // expected are 25 packets, require 9
    if (r < 0)
        return DECODE_ABORT_LENGTH;

    if (bitbuffer->bits_per_row[r] != 25)
        return DECODE_ABORT_LENGTH;
    b = bitbuffer->bb[r];

    // No need to decode/extract values for simple test
    if (!b[0] && !b[1] && !b[2]) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00");
        return DECODE_FAIL_SANITY;
    }

    //invert bits, short pulse is 0, long pulse is 1
    b[0] = ~b[0];
    b[1] = ~b[1];
    b[2] = ~b[2];

    id  = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
    cmd = b[2] & 0x0F;
    switch (cmd) {
    case 0xa: cmd_str = "motion"; break;
    case 0xe: cmd_str = "open"; break;
    case 0x7: cmd_str = "close"; break;
    case 0xb: cmd_str = "tamper"; break;
    case 0x5: cmd_str = "water"; break;
    case 0xf: cmd_str = "battery"; break;
    default: cmd_str = NULL; break;
    }

    if (!cmd_str)
        return DECODE_ABORT_EARLY;

    /* clang-format off */
    data = data_make(
            "model",        "",                 DATA_STRING, "Kerui-Security",
            "id",           "ID (20bit)",       DATA_FORMAT, "0x%x", DATA_INT, id,
            "cmd",          "Command (4bit)",   DATA_FORMAT, "0x%x", DATA_INT, cmd,
            "motion",       "",                 DATA_COND, cmd == 0xa, DATA_INT, 1,
            "opened",       "",                 DATA_COND, cmd == 0xe, DATA_INT, 1,
            "opened",       "",                 DATA_COND, cmd == 0x7, DATA_INT, 0,
            "tamper",       "",                 DATA_COND, cmd == 0xb, DATA_INT, 1,
            "water",        "",                 DATA_COND, cmd == 0x5, DATA_INT, 1,
            "battery_ok",   "Battery",          DATA_COND, cmd == 0xf, DATA_INT, 0,
            "state",        "State",            DATA_STRING, cmd_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "cmd",
        "motion",
        "opened",
        "tamper",
        "water",
        "battery_ok",
        "state",
        NULL,
};

r_device const kerui = {
        .name        = "Kerui PIR / Contact Sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 420,
        .long_width  = 960,
        .gap_limit   = 1100,
        .reset_limit = 9900,
        .tolerance   = 160,
        .decode_fn   = &kerui_callback,
        .fields      = output_fields,
};
