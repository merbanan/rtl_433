/** @file
    Generic off-brand wireless motion sensor and alarm system on 433.3MHz.

    Copyright (C) 2015 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Generic off-brand wireless motion sensor and alarm system on 433.3MHz.

Example codes are: 80042 Arm alarm, 80002 Disarm alarm,
80008 System ping (every 15 minutes), 800a2, 800c2, 800e2 Motion event
(following motion detection the sensor will blackout for 90 seconds).

2315 baud on/off rate and alternating 579 baud bit rate and 463 baud bit rate
Each transmission has a warm-up of 17 to 32 pulse widths then 8 packets with
alternating 1:3 / 2:2 or 1:4 / 2:3 gap:pulse ratio for 0/1 bit in the packet
with a repeat gap of 4 pulse widths, i.e.:
- 6704 us to 13092 us warm-up pulse, 1672 us gap,
- 0: 472 us gap, 1332 us pulse
- 1: 920 us gap, 888 us pulse
- 1672 us repeat gap,
- 0: 472 us gap, 1784 us pulse
- 1: 920 us gap, 1332 us pulse
- ...
*/

#include "decoder.h"

static int scve_door_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        uint8_t *b = bitbuffer->bb[i];

        b[0] = ~b[0];
        b[1] = ~b[1];
        b[2] = ~b[2];

        // strictly validate package as there is no checksum
        if ((bitbuffer->bits_per_row[i] != 25)
                || (b[3] & 0x80) == 0 // Last bit (MSB here) is always 1
                || ((b[0] == 0) && (b[1] == 0))
                || ((b[2] == 0))
                || bitbuffer_count_repeats(bitbuffer, i, 0) < 3)
            continue; // DECODE_ABORT_EARLY

        int code = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
        char code_str[6];
        snprintf(code_str, sizeof(code_str), "%04x", code);
        
        char *cmd;
        switch(b[2] & 0x0f) {
            case 1:
                cmd="Down";
                break;
            case 4:
                cmd="Stop";
                break;
            case 8:
                cmd="Up";
                break;
            default:
                cmd="Unknown";
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",    "",  DATA_STRING, "SCVE Door",
                "id",     "",   DATA_STRING, code_str,
                "button", "",   DATA_STRING, cmd,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }   
    return DECODE_ABORT_EARLY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button",
        NULL,
};

r_device const scve_door = {
        .name        = "scve_door",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 315,
        .long_width  = 945,
        .sync_width  = 0,
        .gap_limit   = 9450,
        .reset_limit = 200000,
        .decode_fn   = &scve_door_callback,
        .fields      = output_fields,
};
