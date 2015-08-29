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
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "pulse_demod.h"


static int chuango_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];
	b[0] = ~b[0];
	b[1] = ~b[1];
	b[2] = ~b[2];

	unsigned bits = bitbuffer->bits_per_row[0];

	// Validate package
	if ((bits == 25)
	 && (b[3] && 0x7F)	// Last bit is always 0
	 && (b[0] != 0x00) && (b[1] != 0x00) && (b[2] != 0x00)	// Reduce false positives. ID 0x00000 not supported
	) {
		uint32_t ID = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4); // ID is 20 bits (Ad: "1 Million combinations" :-)
		char *CMD;
		switch(b[2] & 0x0F) {
			case 0xF:	CMD = "0xF (?)";	break;
			case 0xE:	CMD = "0xE (?)";	break; //tristate 1110 = 1? invalid code!
			case 0xD:	CMD = "0xD (Low Battery)";	break;
			case 0xC:	CMD = "0xC (?)";	break;
			case 0xB:	CMD = "0xB (24H Zone)";	break;
			case 0xA:	CMD = "0xA (Single Delay Zone)";	break;
			case 0x9:	CMD = "0x9 (?)";	break;
			case 0x8:	CMD = "0x8 (Arm)";	break;
			case 0x7:	CMD = "0x7 (Normal Zone)";	break; //tristate 0111 = F1, all floating tristate should be impossible in data part, only valid in address!
			case 0x6:	CMD = "0x6 (Home Mode Zone)";	break; //tristate 0110 = F?, invalid code!
			case 0x5:	CMD = "0x5 (?)";	break; //tristate 0101 = FF
			case 0x4:	CMD = "0x4 (Home Mode)";	break; //tristate 0100 = F0
			case 0x3:	CMD = "0x3 (Tamper)";	break;
			case 0x2:	CMD = "0x2 (Alarm!)";	break;
			case 0x1:	CMD = "0x1 (Disarm)";	break; //tristate 0001 = 0F 
			case 0x0:	CMD = "0x0 (Test)";	break;
			default:	CMD = ""; break;
		}

		uint32_t ID_16b = b[0] << 8 | b[1];
		unsigned char CMD_8b = b[2];

		fprintf(stdout, "Chuango Security Technology\n");
		fprintf(stdout, "ID 20bit = 0x%05X\n", ID);
		fprintf(stdout, "CMD 4bit = %s\n", CMD);
		fprintf(stdout, "ID 16bit = 0x%04X\n", ID_16b);
		fprintf(stdout, "CMD 8bit = 0x%02X\n", CMD_8b);


		// output tristate coding

		uint32_t FULL = b[0] << 16 | b[1] << 8 | b[2];
		char c;

		fprintf(stdout, "TRISTATE = ");
		for (signed char i=22; i>=0; i-=2) {

			switch ((FULL>>i) & 0x03) {
				case 0x00:	c = '0'; break;
				case 0x01:	c = 'F'; break;
				case 0x02:	c = '!'; break; // tristate 10 is invalid code!
				case 0x03:	c = '1'; break;
				default:	c = '?'; break; // not possible anyway
			}

			fputc(c, stdout);
		}
		fprintf(stdout, "\n");

		return 1;
	}
	return 0;
}


PWM_Precise_Parameters pwm_precise_parameters = {
	.pulse_tolerance	= 20,
	.pulse_sync_width	= 0,	// No sync bit used
};

r_device chuango = {
	.name			= "Chuango Security Technology",
	.modulation		= OOK_PULSE_PWM_PRECISE,
	.short_limit	= 142,	// Pulse: Short 142, Long 426 
	.long_limit		= 426,	// Gaps:  Short 142, Long 424
	.reset_limit	= 450,	// Intermessage Gap 4300 (individually for now)
	.json_callback	= &chuango_callback,
	.disabled		= 0,
	.demod_arg		= (unsigned long)&pwm_precise_parameters,
};
