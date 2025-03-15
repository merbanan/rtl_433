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

static int dooya_curtain_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        uint8_t *b = bitbuffer->bb[i];

        b[0] = ~b[0];
        b[1] = ~b[1];
        b[2] = ~b[2];
        b[3] = ~b[3];
        b[4] = ~b[4];

        // strictly validate package as there is no checksum
        if ((bitbuffer->bits_per_row[i] != 40)
                || ((b[0] == 0) && (b[1] == 0) && (b[2] == 0))
                || ((b[3] == 0))
                || ((b[4] == 0))
                || bitbuffer_count_repeats(bitbuffer, i, 0) < 5)
            continue; // DECODE_ABORT_EARLY

        int id = (b[0] << 16) | (b[1] << 8) | b[2];
        char id_str[8];
        snprintf(id_str, sizeof(id_str), "%06x", id);
        
        int channel=b[3];


        char *button;
        switch(b[4] & 0x0f) {
            case 1:
                button="Open";
                break;
            case 3:
                button="Close";
                break;
            case 5:
                button="Stop";
                break;
            default:
                button="Unknown";
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",    "",  DATA_STRING, "Dooya Curtain",
                "id",     "",   DATA_STRING, id_str,
                "channel", "",  DATA_INT, channel,
                "button", "",   DATA_STRING, button,
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
        "channel",
        "button",
        NULL,
};

r_device const dooya_curtain = {
        .name        = "dooya_curtain",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 350,
        .long_width  = 750,
        .sync_width  = 4900,
        .gap_limit   = 990,
        .reset_limit = 9900,
        .disabled   = 0,
        .decode_fn   = &dooya_curtain_callback,
        .fields      = output_fields,
};
