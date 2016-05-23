#include "rtl_433.h"
#include "data.h"
#include "util.h"

// Actually 37 bits for all but last transmission which is 36 bits
#define	NUM_BITS	36

static int springfield_callback(bitbuffer_t *bitbuffer) {
	int ret = 0;
	char time_str[LOCAL_TIME_BUFLEN];
	int row;
	int cs;
	int i;
	int nibble[NUM_BITS/4+1];
	int sid, battery, transmit, channel, temp;
	float tempC;
	int moisture, uk1;
	int checksum;
	data_t *data;
	long tmpData;
	long savData = 0;

	local_time_str(0, time_str);

	for(row = 0; row < bitbuffer->num_rows; row++) {
		if(bitbuffer->bits_per_row[row] == NUM_BITS || bitbuffer->bits_per_row[row] == NUM_BITS + 1) {
			cs = 0;
			tmpData = (bitbuffer->bb[row][0] << 24) + (bitbuffer->bb[row][1] << 16) + (bitbuffer->bb[row][2] << 8) + bitbuffer->bb[row][3];
			for(i = 0; i < (NUM_BITS/4); i++) {
				if((i & 0x01) == 0x01)
					nibble[i] = bitbuffer->bb[row][i >> 1] & 0x0f;
				else
					nibble[i] = bitbuffer->bb[row][i >> 1] >> 0x04;
				if(i < 7) cs ^= nibble[i];
			}
			cs = (cs & 0xF);
			checksum = nibble[7];
			if(checksum == cs && tmpData != savData) {
				savData = tmpData;
				sid      = (nibble[0] << 4) + nibble[1];
				battery  = (nibble[2] >> 3) & 0x01;
				transmit = (nibble[2] >> 2) & 0x01;
				channel  = (nibble[2] & 0x03) + 1;
				temp     = ((nibble[3] << 8) + (nibble[4] << 4) + nibble[5]);
				if(temp >= 0xf00) temp = temp - 0x1000;
				tempC    = temp / 10.0;
				moisture =  nibble[6];
				uk1      =  nibble[8];	/* unknown. */

				data = data_make(
					"time",			"",				DATA_STRING,	time_str,
					"model",		"",				DATA_STRING,	"Springfield Temperature & Moisture",
					"sid",			"SID",			DATA_INT,		sid,
					"channel",		"Channel",		DATA_INT,		channel,
					"battery",		"Battery",		DATA_STRING,	battery ? "LOW" : "OK",
					"transmit",		"Transmit",		DATA_STRING,	transmit ? "MANUAL" : "AUTO",
					"temperature_C","Temperature",	DATA_FORMAT,	"%.01f C",				DATA_DOUBLE,	tempC,
					"moisture",		"Moisture",		DATA_INT,		moisture,
//					"uk1",			"uk1",			DATA_INT,		uk1,
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
  "sid",
  "channel",
  "battery",
  "transmit",
  "temperature_C",
  "moisture",
  NULL
};

r_device springfield = {
  .name           = "Springfield Temperature and Soil Moisture",
  .modulation     = OOK_PULSE_PPM_RAW, 
  .short_limit    = 500 * 4,
  .long_limit     = 1000 * 4, 
  .reset_limit    = 2300 * 4,
  .json_callback  = &springfield_callback,
  .disabled       = 0,
  .demod_arg      = 0,
  .fields         = output_fields
};
