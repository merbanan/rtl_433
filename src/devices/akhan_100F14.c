/* Akhan remote keyless entry system
*
*	This RKE system uses a HS1527 OTP encoder (http://sc-tech.cn/en/hs1527.pdf)
*	Each message consists of a preamble, 20 bit id and 4 data bits.
*
*	(code based on chuango.c and generic_remote.c)
*/
#include "rtl_433.h"
#include "pulse_demod.h"

static int akhan_rke_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];
	
	//invert bits, short pulse is 0, long pulse is 1
	b[0] = ~b[0];
	b[1] = ~b[1];
	b[2] = ~b[2];

	unsigned bits = bitbuffer->bits_per_row[0];

	if (bits == 25) {
		int isAkhan = 1;
		uint32_t ID = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
		uint32_t data = b[2] & 0x0F;
		char *CMD;

		switch (data) {
			case 0x1:	CMD = "0x1 (Lock)"; break;
			case 0x2:	CMD = "0x2 (Unlock)"; break;
			case 0x4:	CMD = "0x4 (Mute)"; break;
			case 0x8:	CMD = "0x8 (Alarm)"; break;
			default:
				isAkhan = 0;
				CMD = "The data received is not used by the akham keyfob.\nThis might be another device using a HS1527 OTP encoder with the same timing.";
				break;
		}

		fprintf(stdout, "Akhan 100F14 remote keyless entry\n");
		fprintf(stdout, "ID (20bit) = 0x%x\n", ID);
		if (isAkhan == 1) {
			fprintf(stdout, "Data (4bit) = %s\n", CMD);
		} else {
			fprintf(stdout, "Data (4bit) = 0x%x\n", data);
			fprintf(stdout, "%s\n", CMD);
		}

		return 1;
	}

	return 0;
}

PWM_Precise_Parameters pwm_precise_parameters_akhan = {
	.pulse_tolerance	= 20,
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
	.demod_arg     = (unsigned long)&pwm_precise_parameters_akhan,
};
