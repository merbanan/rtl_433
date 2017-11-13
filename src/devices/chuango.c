/* Chuango Security Technology Corporation
 *
 * Tested devices:
 * G5 GSM/SMS/RFID Touch Alarm System (Alarm, Disarm, ...)
 * DWC-100 Door sensor (Default: Normal Zone)
 * DWC-102 Door sensor (Default: Normal Zone)
 * KP-700 Wireless Keypad (Arm, Disarm, Home Mode, Alarm!)
 * PIR-900 PIR sensor (Default: Home Mode Zone)
 * RC-80 Remote Control (Arm, Disarm, Home Mode, Alarm!)
 * SMK-500 Smoke sensor (Default: 24H Zone)
 * WI-200 Water sensor (Default: 24H Zone)
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "pulse_demod.h"
#include "data.h"
#include "util.h"


static int chuango_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];
	b[0] = ~b[0];
	b[1] = ~b[1];
	b[2] = ~b[2];

	unsigned bits = bitbuffer->bits_per_row[0];

	// Validate package
	if ((bits == 25)
	 && (b[3] & 0x80)	// Last bit (MSB here) is always 1
	 && (b[0] || b[1] || (b[2] & 0xF0))	// Reduce false positives. ID 0x00000 not supported
	) {
		uint32_t ID = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4); // ID is 20 bits (Ad: "1 Million combinations" :-)
		char *CMD;
		uint32_t CMD_ID = b[2] & 0x0F;
		data_t *data;
		char time_str[LOCAL_TIME_BUFLEN];

		switch(CMD_ID) {
			case 0xF:	CMD = "?"; break;
			case 0xE:	CMD = "?"; break;
			case 0xD:	CMD = "Low Battery"; break;
			case 0xC:	CMD = "?"; break;
			case 0xB:	CMD = "24H Zone"; break;
			case 0xA:	CMD = "Single Delay Zone"; break;
			case 0x9:	CMD = "?"; break;
			case 0x8:	CMD = "Arm"; break;
			case 0x7:	CMD = "Normal Zone"; break;
			case 0x6:	CMD = "Home Mode Zone";	break;
			case 0x5:	CMD = "?"; break;
			case 0x4:	CMD = "Home Mode"; break;
			case 0x3:	CMD = "Tamper";	break;
			case 0x2:	CMD = "Alarm"; break;
			case 0x1:	CMD = "Disarm";	break;
			case 0x0:	CMD = "Test"; break;
			default:	CMD = ""; break;
		}
		local_time_str(0, time_str);
		data = data_make("time", "", DATA_STRING, time_str,
			"model", "", DATA_STRING, "Chuango Security Technology",
			"id", "ID", DATA_INT, ID,
			"cmd", "CMD", DATA_STRING, CMD,
			"cmd_id", "CMD_ID", DATA_INT, CMD_ID,
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
	"cmd",
	"cmd_id",
	NULL
};

PWM_Precise_Parameters pwm_precise_parameters = {
	.pulse_tolerance	= 160, // us
	.pulse_sync_width	= 0,	// No sync bit used
};

r_device chuango = {
	.name			= "Chuango Security Technology",
	.modulation		= OOK_PULSE_PWM_PRECISE,
	.short_limit	= 568,	// Pulse: Short 568µs, Long 1704µs
	.long_limit		= 1704,	// Gaps:  Short 568µs, Long 1696µs
	.reset_limit	= 1800,	// Intermessage Gap 17200µs (individually for now)
	.json_callback	= &chuango_callback,
	.disabled		= 0,
	.demod_arg		= (uintptr_t)&pwm_precise_parameters,
};
