/** @file
    Dickert MAHS433-01 remote control

    Copyright (C) 2024 daubsi

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

// TODO: Add sample and textual description of protocol


static int dickert_mahs_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t b[40];
    data_t *data;
    char dips[40] = {0}; // changed from 20 to 40
    char dips_s[40] = {0}; // changed from 20 to 40
    char facs_s[40] = {0}; // changed from 20 to 40

    const int MSG_LEN = 37;

    enum SwitchPos {
	 PLUS  = 3, // 0b11
	 ZERO  = 1, // 0b01
	 MINUS = 0, // 0x00
    };

    enum SwitchPos dip;

    // We only expect one row per transmission
    if (bitbuffer->num_rows != 1) {
    	return DECODE_ABORT_EARLY;
    }

    // printf("Length: %d\n", bitbuffer->bits_per_row[0]);
    if (bitbuffer->bits_per_row[0] != MSG_LEN) {
	return DECODE_ABORT_LENGTH;
    }

    bitbuffer_print(bitbuffer);

    // Remove the first bit and store in b
    bitbuffer_extract_bytes(bitbuffer, 0, 1, b, MSG_LEN);

    // Get dip switches
    for (int idx=0; idx<9; idx++) { // changed from 5 to 9
        uint8_t byte = b[idx];

	for (int nib=3; nib>=0; nib--) {
                dip = (enum SwitchPos) ((byte >> (2*nib)) & 3);
		dips[idx * 4 + (3-nib)] = dip == PLUS ? '+' : dip == ZERO ? '0' : dip == MINUS ? '-' : '?';
	}
    }
	
    // Extract the 10 first switch positions
    // We intentionally disable the stringop-truncation warning as we're sure dips_s will be \0-terminated as it's initialized
    // with 0 and > 10 chars

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wstringop-truncation"
    strncat(dips_s, dips, 10); // changed from 10 to 18
    strncat(facs_s, dips+10, 8); // changed from 10 to 18
    #pragma GCC diagnostic pop

    /* clang-format off */
    data = data_make(
            "model",        "",      DATA_STRING, "Dickert MAHS433-01 remote control",
            "dipswitches",  "DIP switches configuration", DATA_STRING, dips_s,
            "facswitches",  "Factory code", DATA_STRING, facs_s,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "dipswitches",
	"facswitches",
        NULL,
};

r_device const dickert_mahs = {
        .name        = "Dickert MAHS433-01 garage door remote control",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 362,
        .long_width  = 770,
        .gap_limit   = 1064,
        .reset_limit = 12000,
	.disabled    = 1,
        .decode_fn   = &dickert_mahs_decode,
        .fields      = output_fields,
};

