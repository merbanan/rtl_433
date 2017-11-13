/* Akhan remote keyless entry system
*
*	This RKE system uses a HS1527 OTP encoder (http://sc-tech.cn/en/hs1527.pdf)
*	Each message consists of a preamble, 20 bit id and 4 data bits.
*
*	(code based on chuango.c and generic_remote.c)
*/
#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"
#include "data.h"

static int akhan_rke_callback(bitbuffer_t *bitbuffer) {
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
		int isAkhan = 1;
		char *CMD;

		switch (dataBits) {
			case 0x1:	CMD = "0x1 (Lock)"; break;
			case 0x2:	CMD = "0x2 (Unlock)"; break;
			case 0x4:	CMD = "0x4 (Mute)"; break;
			case 0x8:	CMD = "0x8 (Alarm)"; break;
			default:
				isAkhan = 0;
				break;
		}

		if (isAkhan == 1) {
			data = data_make(	"time",		"",				DATA_STRING,	time_str,
									"model",	"",				DATA_STRING,	"Akhan 100F14 remote keyless entry",
									"id",			"ID (20bit)",	DATA_FORMAT, 	"0x%x", 	DATA_INT, ID,
									"data",		"Data (4bit)",	DATA_STRING,	CMD,
									NULL);

			data_acquired_handler(data);
			return 1;
		}

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

PWM_Precise_Parameters pwm_precise_parameters_akhan = {
	.pulse_tolerance	= 80, // us
	.pulse_sync_width	= 0,
};

r_device akhan_100F14 = {
	.name          = "Akhan 100F14 remote keyless entry",
	.modulation    = OOK_PULSE_PWM_PRECISE,
	.short_limit   = 316,
	.long_limit    = 1020,
	.reset_limit   = 1800,
	.json_callback = &akhan_rke_callback,
	.disabled      = 0,
	.demod_arg     = (uintptr_t)&pwm_precise_parameters_akhan,
};
