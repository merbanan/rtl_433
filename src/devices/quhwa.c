/* Quhwa
 *
 * Tested devices:
 * QH-C-CE-3V (which should be compatible with QH-832AC),
 * also sold as "1 by One" wireless doorbell
 *
 * Copyright (C) 2016 Ask Jakobsen
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "pulse_demod.h"
#include "data.h"
#include "util.h"


static int quhwa_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];

	b[0] = ~b[0];
	b[1] = ~b[1];
	b[2] = ~b[2];

	unsigned bits = bitbuffer->bits_per_row[0];

	if ((bits == 18) &&
		((b[1] & 0x03) == 0x03) &&
		((b[2] & 0xC0) == 0xC0))
	{
		uint32_t ID = (b[0] << 8) | b[1];
		data_t *data;

		char time_str[LOCAL_TIME_BUFLEN];
		local_time_str(0, time_str);

		data = data_make("time", "", DATA_STRING, time_str,
			"model", "", DATA_STRING, "Quhwa doorbell",
			"id", "ID", DATA_INT, ID,
			NULL);

		data_acquired_handler(data);

		return 1;
	}
	return 0;
}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	NULL
};

PWM_Precise_Parameters pwm_precise_parameters_quhwa = {
	.pulse_tolerance	= 80, // us
	.pulse_sync_width	= 0,	// No sync bit used
};

r_device quhwa = {
	.name			= "Quhwa",
	.modulation		= OOK_PULSE_PWM_PRECISE,
	.short_limit            = 360,	// Pulse: Short 360µs, Long 1070µs
	.long_limit		= 1070,	// Gaps: Short 360µs, Long 1070µs
	.reset_limit	        = 1200,	// Intermessage Gap 5200µs
	.json_callback	        = &quhwa_callback,
	.disabled		= 0,
	.demod_arg		= (uintptr_t)&pwm_precise_parameters_quhwa,
};
