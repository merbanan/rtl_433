/* Oil tank monitor using Si4320 framed FSK protocol
 *
 * Tested devices:
 * Sensor Systems Watchman Sonic
 *
 * Copyright © 2015 David Woodhouse
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

static int oil_watchman_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];
	uint32_t unit_id;
	uint16_t depth = 0;
	uint16_t binding_countdown = 0;
	uint8_t flags;
	uint8_t maybetemp;
	time_t time_now;
	char time_str[LOCAL_TIME_BUFLEN];
	data_t *data;

	if (bitbuffer->bits_per_row[0] != 64 ||
		b[0] != 0x28 || b[7] != crc8le(b, 7, 0x31, 0))
			return 0;

	time(&time_now);
	local_time_str(time_now, time_str);

	// The unit ID changes when you rebind by holding a magnet to the
	// sensor for long enough; it seems to be time-based.
	unit_id = (b[1] << 16) | (b[2] << 8) | b[3];

	// 0x01: Rebinding (magnet held to sensor)
	// 0x08: Leak/theft alarm
	flags = b[4];

	// Not entirely sure what this is but it might be inversely
	// proportional to temperature.
	maybetemp = b[5] >> 2;

	if (flags & 1)
			// When binding, the countdown counts up from 0x51 to 0x5a
			// (as long as you hold the magnet to it for long enough)
			// before the device ID changes. The receiver unit needs
			// to receive this *strongly* in order to change its
			// allegiance.
			binding_countdown = b[6];
	else
			// A depth reading of zero indicates no reading. Even with
			// the sensor flat down on a table, it still reads about 13.
			depth = b[6] | ((((uint16_t)b[5]) & 3) << 8);

	data = data_make("time", "", DATA_STRING, time_str,
			 "model", "", DATA_STRING, "Oil Watchman",
			 "id", "", DATA_FORMAT, "%06x", DATA_INT, unit_id,
			 "flags", "", DATA_FORMAT, "%02x", DATA_INT, flags,
			 "maybetemp", "", DATA_INT, maybetemp,
		     "binding_countdown", "", DATA_INT, binding_countdown,
		     "depth", "", DATA_INT, depth,
		     NULL);
    data_acquired_handler(data);

	return 0;
};

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"flags",
	"maybetemp",
	"binding_countdown",
	"depth",
	NULL
};

r_device oil_watchman = {
	.name			= "Ultrasonic oil monitor",
	.modulation		= FSK_PULSE_MANCHESTER_FRAMED,
	.short_limit	= 250,
	.json_callback	= &oil_watchman_callback,
	.disabled		= 0,
	.fields			= output_fields,
};
