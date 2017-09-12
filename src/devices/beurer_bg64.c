/* Beurer BG64 Scale
 *
 * Copyright © 2017 John Jore
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * A packet may look like this:
 * Position: 00 01 02 03 04 05 06 07 08 09 10
 * Packet:   ff ff ff ff 52 cf fc ff 00 49 ff  (Normal packet, 81.4kg, with infinate resistance)
 *           ff ff ff ff 52 d1 fc ff 00 79 ff  (Normal packet, 81.6kg, with infinate resistance)
 *           ff ff ff ff 52 cf fc 78 ff 39 ff  (Normal packet, with "normal" resistance) 
 * Inverted: 00 00 00 00 AD 2E 03 00 FF 86 00  (Normal packet, 81.4kg with infinate resistance)
 * 00 to 03: Preamble, ffh inverted
 * 04: Upper half nibble is preamble, 5h (inverted)
 * 04: Lower half nibble is unknown. Battery status?
 * 05 06: Weight in hectograms, reverse order, 032Eh = 814 hectograms = 81.4kg
 * 07 08: Impedance? reverse order ff78h = 0087h = 135 ohm? Probably needs scaling by approx 5.5.
 * 09: Upper half nibble is checksum; invert numbers, add each nibble from 04 to 08 together, modulus 16
 * 09: Lower half nibble is postamble, 9h (inverted)
 * 10: Postamble, ffh inverted
 *
 * Beurer support, kd@beurer.de, stated these are used for the calculations on the remote display:
 *
 * Water:
 *   Limitations in Anthropometric Calculations of Total Body Water, journal of the American Society of Nephrology, Copyright 2001
 * Muscle:
 *   Estimation of skeletal muscle mass by bioelectrical impedance analysis, J. Appl. Physiol, Copyright 2000
 * Bone:
 *   Body composition following hemodialysis: studies using dual energy X ray absorptiometry and bioelectrical impedance analysis.
 *   Osteoporos Int., Copyright 1993
 *   Abnormal body composition and reduced bone mass in growth hormone deficient hypopituitary adults.
 *   Clin Endocrinol (Oxf), Copyright 1995
 *
 * However, when working through the formula for "bone", my calculations do not match what the display shows, 
 * ~1.44% different for body fat calculations.
  *
 * Beurer support could/would not provide any additional clarifications;
 *   Please note that due to our software of the scale and tolerances, deviations may occur.
 *   Please understand that we can not provide you with any further information beyond the data mentioned above
*/

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

#define MODEL "Beurer BG64"

static int beurer_bg64_callback(bitbuffer_t *bitbuffer) {
	data_t *data;	
	int valid = 0;
		
	char time_str[LOCAL_TIME_BUFLEN];
	local_time_str(0, time_str);
	bitrow_t *bb = bitbuffer->bb;
	
	//Loop through each row of data
	for (int i = 0; i < bitbuffer->num_rows; i++)
	{
	        //Correct packet length?
		if (bitbuffer->bits_per_row[i] != 88)
			continue;
		
		//Valid packet? (Note; the data still needs inverting this point)
		//The checksum calculations will check the rest of the packet
		if ((bb[i][9] & 0x0F) != 0x9 || bb[i][10] != 0xff)
			continue;

                //Invert these numbers as we need them multiple times.
                for (int j=0; j<=9; j++) {
                        bb[i][j] = ~(bb[i][j]);
                }

                //Checksum, add each nibble, modulus 16 is highest nibble of column 9
                if (( (bb[i][0] >> 4) + (bb[i][0] & 0xf) + (bb[i][1] >> 4) + (bb[i][1] & 0xf) + 
                      (bb[i][2] >> 4) + (bb[i][2] & 0xf) + (bb[i][3] >> 4) + (bb[i][3] & 0xf) + 
                      (bb[i][4] >> 4) + (bb[i][4] & 0xf) + (bb[i][5] >> 4) + (bb[i][5] & 0xf) + 
                      (bb[i][6] >> 4) + (bb[i][6] & 0xf) + (bb[i][7] >> 4) + (bb[i][7] & 0xf) + 
                      (bb[i][8] >> 4) + (bb[i][8] & 0xf) ) % 16 != (bb[i][9] >> 4) )
		  continue;

		//bitbuffer_print(bitbuffer);
		//fprintf(stdout, "\n");
		
		//Data
		data = data_make(
			"time", "", DATA_STRING, time_str,
			"type", "", DATA_STRING, "Scale",
			"model", "", DATA_STRING, MODEL,
			"weight_kg", "Weight in kg", DATA_FORMAT, "%.1f", DATA_DOUBLE, (float)((uint16_t)((bb[i][6]) << 8) + (uint8_t)(bb[i][5])) / 10,
                        "impedance", "", DATA_INT, ((uint16_t)((bb[i][8]) << 8) + (uint8_t)(bb[i][7])),
			NULL);
		data_acquired_handler(data);
				
		valid++;
    }

    // Return 1 if message successfully decoded
    if (valid)
	return 1;

    return 0;
}

static char *output_fields[] = {
	"time",
	"type",
	"model",
	"weight_kg",
	"impedance",
	NULL
};

r_device beurer_bg64 = {
	.name          = MODEL,
	.modulation    = OOK_PULSE_PWM_RAW,
	.short_limit   = (192+141)/2*4,
	.long_limit    = (192+141)*4,
	.reset_limit   = (192+141)*2*4,
	.json_callback = &beurer_bg64_callback,
	.disabled      = 0,
	.demod_arg     = 0,
	.fields        = output_fields,
};
