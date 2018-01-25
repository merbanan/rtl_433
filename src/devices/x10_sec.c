/*
 * X10 Security sensor decoding for rtl_433
 *
 * Based on code provided by Willi 'wherzig' in issue #30 (2014-04-21)
 * https://github.com/merbanan/rtl_433/issues/30
 * Danke, Willi
 *
 * Tested with American sensors operating at 310 MHz
 * e.g., rtl_433 -f 310.558M
 *
 * This is pretty rudimentary, and I bet the byte value decoding, based
 * on limited observations, doesn't take into account bits that might
 * be set to indicate something like a low battery condition.
 *
 * Copyright (C) 2018 Anthony Kava
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "rtl_433.h"
#include "data.h"
#include "util.h"

static int x10_sec_callback(bitbuffer_t *bitbuffer)
{
	bitrow_t *bb = bitbuffer->bb;		
	data_t *data;

	char *remarks_str="UNKNOWN";		/* human-readable remarks */
	char time_str[LOCAL_TIME_BUFLEN];
	char x10_id_str[5]="\0";			/* string showing hex value */
	char x10_code_str[5]="\0";			/* string showing hex value */

	/* validate what we received ...
	 * used intermediate variables test_a and test_b to suppress
	 * warnings generated when compiling using:
	 *
	 *   if((bb[1][0]^0x0f)==bb[1][1] && (bb[1][2]^0xff)==bb[1][3])
	 */
	uint8_t test_a=bb[1][0]^0x0f;
	uint8_t test_b=bb[1][2]^0xff;
	if(test_a==bb[1][1] && test_b==bb[1][3])
	{
		/* set remarks based on code received */
        switch(bb[1][2])
		{
			case 0x04:
				remarks_str="DS10A DOOR/WINDOW OPEN";
                break;
			case 0x06:
				remarks_str="KR10A KEY-FOB ARM";
                break;
			case 0x0c:
				remarks_str="MS10A MOTION TRIPPED";
                break;
			case 0x46:
				remarks_str="KR10A KEY-FOB LIGHTS-ON";
                break;
			case 0x82:
				remarks_str="SH624 SEC-REMOTE DISARM";
                break;
            case 0x84:
                remarks_str="DS10A DOOR/WINDOW CLOSED";
                break;
			case 0x86:
				remarks_str="KR10A KEY-FOB DISARM";
                break;
			case 0x88:
				remarks_str="KR15A PANIC";
                break;
			case 0x8c:
				remarks_str="MS10A MOTION READY";
                break;
			case 0x98:
				remarks_str="KR15A PANIC-3SECOND";
                break;
			case 0xc6:
				remarks_str="KR10A KEY-FOB LIGHTS-OFF";
                break;
		}

		/* get x10_id_str, x10_code_str, and time_str ready for output */
		sprintf(x10_id_str,"%02x%02x",bb[1][0],bb[1][4]);
		sprintf(x10_code_str,"%02x",bb[1][2]);
		local_time_str(0, time_str);

		/* debug output */
		if(debug_output)
		{
			fprintf(stderr, "%20s  X10SEC: id = %02x%02x code=%02x remarks_str=%s\n",time_str,bb[1][0],bb[1][4],bb[1][2],remarks_str);
			bitbuffer_print(bitbuffer);
		}

		/* build and handle data set for normal output */
		data=data_make
		(
			"time",		"",				DATA_STRING, time_str,
			"model",	"",				DATA_STRING, "X10 Security",
			"id",		"Device ID",	DATA_STRING, x10_id_str,
			"code",		"Code",			DATA_STRING, x10_code_str,
			"remarks",	"Remarks",		DATA_STRING, remarks_str,
			NULL
		);
    	data_acquired_handler(data);

		return 1;
	}
	return 0;
}

static char *output_fields[] =
{
    "time",
    "model",
    "id",
    "code",
    "remarks",
    NULL
};

/* r_device definition */
r_device x10_sec =
{
	.name			= "X10 Security",
	.modulation		= OOK_PULSE_PPM_RAW,	/* new equivalent to OOK_PWM_D */
	.short_limit	= 1100,
	.long_limit		= 2200,
	.reset_limit	= 10000,
	.json_callback	= &x10_sec_callback,
	.disabled		= 1,
	.demod_arg		= 0,
    .fields         = output_fields
};
