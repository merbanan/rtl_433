/* Kerui Door/Window sensor
*
*	Code derived from kerui.c
*
*/

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"
#include "data.h"

static int kerui_door_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	uint8_t *b = bb[0];

	unsigned bits = bitbuffer->bits_per_row[0];

	if (bits == 25) {
		char time_str[LOCAL_TIME_BUFLEN];
		local_time_str(0, time_str);
		data_t *data;
		uint32_t ID = (b[3] << 24) + (b[2] << 16) + (b[1] << 8) + b[0];
	
		data = data_make(	"time",		"",				DATA_STRING,	time_str,
							"model",	"",				DATA_STRING,	"Kerui Door Sensor",
							"id",			"ID",	DATA_FORMAT, 	"0x%x", 	DATA_INT, ID ,
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
	NULL
};

r_device kerui_door = {
	.name          = "Kerui Door Sensor",
	.modulation    = OOK_PULSE_PWM_PRECISE,
	.short_limit   = 303,
	.long_limit    = 888,
	.reset_limit   = 8000,
	.json_callback = &kerui_door_callback,
	.disabled      = 0,
	.tolerance     = 80,
	.sync_width = 0
};
