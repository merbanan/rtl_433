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


static int quhwa_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];
	b[0] = ~b[0];
	b[1] = ~b[1];
	b[2] = ~b[2];
	

	unsigned bits = bitbuffer->bits_per_row[0];


	/* 61 bb c0 */ /* http://goughlui.com/2013/12/20/rtl-sdr-433-92mhz-askook-decoding-of-various-devices-with-rtl_433/ */
	/* e9 3b c0 */
	/* 76 0b c0 */
	/* b8 03 c0 */ /* MY! */
	

	if ((bits == 18) && (b[2]==0xFF)  && (b[1] & 1) && (b[1] & 2))
	  {
	    uint32_t ID = (b[0] << 6) | (b[1] >> 2);
	    
	    fprintf(stdout, "Quhwa doorbell\n");
	    fprintf(stdout, "ID 14bit = 0x%04X\n", ID);
	    return 1;
	}
	return 0;
}


PWM_Precise_Parameters pwm_precise_parameters_quhwa = {
	.pulse_tolerance	= 20,
	.pulse_sync_width	= 0,	// No sync bit used
};

r_device quhwa = {
	.name			= "Quhwa",
	.modulation		= OOK_PULSE_PWM_PRECISE,
	.short_limit	= 360,	// Pulse: Short 568µs, Long 1704µs 
	.long_limit		= 1070,	// Gaps:  Short 568µs, Long 1696µs
	.reset_limit	= 1200,	// Intermessage Gap 17200µs (individually for now)
	.json_callback	= &quhwa_callback,
	.disabled		= 0,
	.demod_arg		= (unsigned long)&pwm_precise_parameters_quhwa,
};
