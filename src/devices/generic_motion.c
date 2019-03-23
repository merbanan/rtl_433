/* Generic off-brand wireless motion sensor and alarm system on 433.3MHz
 *
 * Example codes are: 80042 Arm alarm, 80002 Disarm alarm,
 * 80008 System ping (every 15 minutes), 800a2, 800c2, 800e2 Motion event
 * (following motion detection the sensor will blackout for 90 seconds).
 *
 * 2315 baud on/off rate and alternating 579 baud bit rate and 463 baud bit rate
 * Each transmission has a warmup of 17 to 32 pulse widths then 8 packets with
 * alternating 1:3 / 2:2 or 1:4 / 2:3 gap:pulse ratio for 0/1 bit in the packet
 * with a repeat gap of 4 pulse widths, i.e.:
 * 6704 us to 13092 us warmup pulse, 1672 us gap,
 * 0: 472 us gap, 1332 us pulse
 * 1: 920 us gap, 888 us pulse
 * 1672 us repeat gap,
 * 0: 472 us gap, 1784 us pulse
 * 1: 920 us gap, 1332 us pulse
 * ...
 *
 * Copyright (C) 2015 Christian W. Zuckschwerdt <zany@triq.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static int generic_motion_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t *b;
    int code;
    char code_str[6];

    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        b = bitbuffer->bb[i];
        // strictly validate package as there is no checksum
        if ((bitbuffer->bits_per_row[i] != 20)
                || ((b[1] == 0) && (b[2] == 0))
                || count_repeats(bitbuffer, i) < 3)
            continue;

        code = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
        sprintf(code_str, "%05x", code);

        data = data_make(
                "model",    "",  DATA_STRING, _X("Generic-Motion","Generic motion sensor"),
                "code",     "",  DATA_STRING, code_str,
                NULL);

        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "model",
    "code",
    NULL
};

r_device generic_motion = {
    .name           = "Generic wireless motion sensor",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 888,
    .long_width     = (1332+1784)/2,
    .sync_width     = 1784+670,
    .gap_limit      = 1200,
    .reset_limit    = 2724*1.5,
    .decode_fn      = &generic_motion_callback,
    .disabled       = 0,
    .fields         = output_fields
};
