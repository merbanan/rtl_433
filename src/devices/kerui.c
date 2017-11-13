/* Kerui PIR sensor
*
*	Code derrived from akhan_100F14.c
*
* Such as
* http://www.ebay.co.uk/sch/i.html?_from=R40&_trksid=p2050601.m570.l1313.TR0.TRC0.H0.Xkerui+pir.TRS0&_nkw=kerui+pir&_sacat=0
*/

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"
#include "data.h"

static int kerui_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];

	//invert bits, short pulse is 0, long pulse is 1
	b[0] = ~b[0];
	b[1] = ~b[1];
	b[2] = ~b[2];

	unsigned bits = bitbuffer->bits_per_row[0];

	if (bits == 25) {
		char time_str[LOCAL_TIME_BUFLEN];
		local_time_str(0, time_str);
		data_t *data;

		uint32_t ID = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
		uint32_t dataBits = b[2] & 0x0F;
		int isKerui = 1;
		char *CMD;

		switch (dataBits) {
			case 0xa:	CMD = "0xa (PIR)"; break;
			default:
				isKerui = 0;
				break;
		}

		if (isKerui == 1) {
			data = data_make(	"time",		"",				DATA_STRING,	time_str,
									"model",	"",				DATA_STRING,	"Kerui PIR Sensor",
									"id",			"ID (20bit)",	DATA_FORMAT, 	"0x%x", 	DATA_INT, ID,
									"data",		"Data (4bit)",	DATA_STRING,	CMD,
									NULL);

		} else {
			return 0;
		}

		data_acquired_handler(data);
		return 1;
	}
	return 0;
}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"data",
	NULL
};

PWM_Precise_Parameters pwm_precise_parameters_kerui = {
	.pulse_tolerance	= 80, // us
	.pulse_sync_width	= 0,
};

r_device kerui = {
	.name          = "Kerui PIR Sensor",
	.modulation    = OOK_PULSE_PWM_PRECISE,
	.short_limit   = 316,
	.long_limit    = 1020,
	.reset_limit   = 1800,
	.json_callback = &kerui_callback,
	.disabled      = 0,
	.demod_arg     = (uintptr_t)&pwm_precise_parameters_kerui,
};
