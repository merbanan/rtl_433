#include "rtl_433.h"
#include <stdio.h>

static int kangtai_callback(bitbuffer_t *bitbuffer) {
	uint8_t *bytes = bitbuffer->bb[0];
	data_t *data;
	uint8_t k0, k1, k2, k3, k4, k5;
	uint8_t g0, g1, g2, g3, g4, g5;
	char table1[] = { 1, 8, 4, 14, 2, 7, 13, 6, 15, 12, 0, 10, 3, 11, 5, 9 };
	char table2[] = { 15, 6, 0, 11, 5, 2, 10, 4, 12, 13, 14, 8, 1, 9, 3, 7 };
	uint16_t address;

	if (24 != bitbuffer->bits_per_row[0]) return 0;

	bitbuffer_invert(bitbuffer);

	k5 = (bytes[0] >> 4) & 0x0F;
	k4 = bytes[0] & 0x0F;
	k3 = (bytes[1] >> 4) & 0x0F;
	k2 = bytes[1] & 0x0F;
	k1 = (bytes[2] >> 4) & 0x0F;
	k0 = bytes[2] & 0x0F;

	if ((k5 & 2) == 0) {
		g5 = k5 ^ 9;
		g4 = table1[k4] ^ k3;
		g3 = table1[k3] ^ k2;
		g2 = table1[k2] ^ k1;
		g1 = table1[k1] ^ k0;
		g0 = table1[k0];
	} else {
		g5 = k5 ^ 9;
		g4 = table2[k4] ^ k3;
		g3 = table2[k3] ^ k2;
		g2 = table2[k2] ^ k1;
		g1 = table2[k1] ^ k0;
		g0 = table2[k0];
	}

	bytes[0] = (g5 << 4) | g4;
	bytes[1] = (g3 << 4) | g2;
	bytes[2] = (g1 << 4) | g0;

	address = (bytes[0] << 8) | bytes[1];

	data = data_make(
		"model", "Model", DATA_STRING, "Kangtai",
		"address", "Address", DATA_FORMAT, "%x", DATA_INT, address,
		"loop", "Loop", DATA_FORMAT, "%d", DATA_INT, ((bytes[2] >> 6) & 3),
		"command", "Command", DATA_STRING, ((bytes[2] >> 5) & 0x01) ? "on" : "off",
		"unit", "Unit", DATA_FORMAT, "%d", DATA_INT, (bytes[2] & 0x1F),
		NULL
	);
	data_acquired_handler(data);
	return 1;
}

static char *output_fields[] = {
	"model",
	"address",
	"loop",
	"command",
	"unit",
	NULL
};

r_device kangtai = {
	.name           = "Kangtai",
	.modulation     = OOK_PULSE_PWM_RAW,
	.short_limit    = 700,
	.long_limit     = 1400,
	.reset_limit    = 1400,
	.json_callback  = &kangtai_callback,
	.disabled       = 0,
	.demod_arg      = 0,
	.fields         = output_fields
};
