/* Quhwa
 *
 * Tested devices:
 * QH-C-CE-3V (which should be compatible with QH-832AC)
 *
 * Copyright (C) 2016 Ask Jakobsen
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

	if ((bits == 18) &&
	    (b[2]==0xFF) &&
	    (b[1] & 1) && (b[1] & 2)) // Last two bits are one
	  {
	    uint32_t ID = (b[0] << 8) | b[1];
	    
	    fprintf(stdout, "Quhwa\n");
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
