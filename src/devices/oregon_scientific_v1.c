#include "rtl_433.h"
#include "data.h"
#include "util.h"

#define	OSV1_BITS	32

static int rev_nibble(int nib)
{
	int revnib = 0;

	revnib += (nib >> 3) & 0x1;
	revnib += (nib >> 1) & 0x2;
	revnib += (nib << 1) & 0x4;
	revnib += (nib << 3) & 0x8;

	return(revnib);
}

static int oregon_scientific_callback_v1(bitbuffer_t *bitbuffer) {
	int ret = 0;
	char time_str[LOCAL_TIME_BUFLEN];
	int row;
	int cs;
	int i;
	int nibble[OSV1_BITS/4];
	int sid, channel, uk1;
	float tempC;
	int battery, uk2, sign, uk3, checksum;
	data_t *data;

	local_time_str(0, time_str);

	for(row = 0; row < bitbuffer->num_rows; row++) {
		if(bitbuffer->bits_per_row[row] == OSV1_BITS) {
			cs = 0;
			for(i = 0; i < OSV1_BITS / 8; i++) {
				nibble[i * 2    ] = rev_nibble((bitbuffer->bb[row][i] >> 4));
				nibble[i * 2 + 1] = rev_nibble((bitbuffer->bb[row][i] & 0x0f));
				if(i < ((OSV1_BITS / 8) - 1))
					cs += nibble[i * 2] + 16 * nibble[i * 2 + 1];
			}
			cs = (cs & 0xFF) + (cs >> 8);
			checksum = nibble[6] + (nibble[7] << 4);
			if(checksum == cs) {
				sid      = nibble[0];
				channel  = ((nibble[1] >> 2) & 0x03) + 1;
				uk1      = (nibble[1] >> 0) & 0x03;	/* unknown.  Seen change every 60 minutes */
				tempC    =  nibble[2] / 10. + nibble[3] + nibble[4] * 10.;
				battery  = (nibble[5] >> 3) & 0x01;
				uk2      = (nibble[5] >> 2) & 0x01;	/* unknown.  Always zero? */
				sign     = (nibble[5] >> 1) & 0x01;
				uk3      = (nibble[5] >> 0) & 0x01;	/* unknown.  Always zero? */

				if(sign) tempC = -tempC;

				data = data_make(
					"time",			"",				DATA_STRING,	time_str,
					"model",		"",				DATA_STRING,	"OSv1 Temperature Sensor",
					"sid",			"SID",			DATA_INT,		sid,
					"channel",		"Channel",		DATA_INT,		channel,
					"battery",		"Battery",		DATA_STRING,	battery ? "LOW" : "OK",
					"temperature_C","Temperature",	DATA_FORMAT,	"%.01f C",				DATA_DOUBLE,	tempC,
					NULL);
				data_acquired_handler(data);
				ret++;
			}
		}
	}
	return ret;
}

static char *output_fields[] = {
  "time",
  "model",
  "id",
  "channel",
  "battery",
  "temperature_C",
  NULL
};

r_device oregon_scientific_v1 = {
  .name           = "OSv1 Temperature Sensor",
  .modulation     = OOK_PULSE_PWM_OSV1, 
  .short_limit        = 300,
  .long_limit         = 430, 
  .reset_limit        = 14000,
  .json_callback  = &oregon_scientific_callback_v1,
  .disabled       = 0,
  .demod_arg      = 0,
  .fields         = output_fields
};
