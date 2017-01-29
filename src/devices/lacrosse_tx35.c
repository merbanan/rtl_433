
#include "rtl_433.h"
#include "util.h"
#include "data.h"

#define LACROSSE_TX29_NOHUMIDSENSOR  0x6a // Sensor do not support humidty
#define LACROSSE_TX29_CRC_POLY       0x31
#define LACROSSE_TX29_CRC_INIT       0x00

// LaCrosse TX29IT,  Temperature and Humidity Sensors. Weather station WS-9160IT
// Temperature and Humidity are sent in different messages bursts.
static int lacrossetx35_callback(bitbuffer_t *bitbuffer) {
	char time_str[LOCAL_TIME_BUFLEN];
	uint8_t *bb;
    uint16_t brow, row_nbytes;
	uint8_t msg_type, r_crc, c_crc;
	int valid = 0;
	data_t *data;
	int events = 0;	
	
	static const uint8_t preamble[] = {
      0xaa, // preamble
      0x2d, // brand identifer
      0xd4, // brand identifier
	  0x90, // data length (this decoder work only with data length of 9, so we hardcode it on the preamble)
    };
	
	uint8_t out[8] = {0}; // array of byte to decode
	
	local_time_str(0, time_str);
	for (brow = 0; brow < bitbuffer->num_rows; ++brow) {
		bb = bitbuffer->bb[brow];
		
		// Validate message and reject it as fast as possible : check for preamble
		unsigned int start_pos = bitbuffer_search(bitbuffer, brow, 0, preamble, 28);		
		if(start_pos == bitbuffer->bits_per_row[brow])
			continue; // no preamble detected, move to the next row
		if (debug_output >= 1)
			fprintf(stderr, "LaCrosse TX29 detected, buffer is %d bits length\n", bitbuffer->bits_per_row[brow]);
		// remove preamble and keep only 64 bits
		bitbuffer_extract_bytes(bitbuffer, brow, start_pos, out, 64);

		/*
		 * Check message integrity (CRC/Checksum/parity)
		 * Normally, it is computed on the whole message, from byte 0 (preamble) to byte 6,
		 * but preamble is always the same, so we can speed the process by doing a crc check
		 * only on byte 3,4,5,6
		 */		
		r_crc = out[7];
		c_crc = crc8(&out[3], 4, LACROSSE_TX29_CRC_POLY, LACROSSE_TX29_CRC_INIT);
		if (r_crc != c_crc) {
			// example debugging output
			if (debug_output >= 1)
				fprintf(stderr, "%s LaCrosseTX35 bad CRC: calculated %02x, received %02x\n",
					time_str, c_crc, r_crc);
			// reject row
			continue;
		}		
		/*
		 * Now that message "envelope" has been validated,
		 * start parsing data.
		 */
	
		uint8_t sensor_id, newbatt, battery_low;
		uint8_t humidity; // in %. If > 100 : no humidity sensor
		float temp_c; // in °C
		sensor_id = ((out[3] & 0x0f) << 2) | ((out[4]>>6) & 0x03);
		temp_c = 10.0 * (out[4] & 0x0f) +  1.0 *((out[5]>>4) & 0x0f) + 0.1 * (out[5] & 0x0f) - 40.0;
		newbatt = (out[4] >> 5) & 1;
		battery_low = (out[6]>>7) & 1;
		humidity = 1 * (out[6] & 0x7F);
		if (humidity == LACROSSE_TX29_NOHUMIDSENSOR) 
			humidity = -1;
		
			data = data_make("time",          "",            DATA_STRING, time_str,
							 "brand",         "",            DATA_STRING, "LaCrosse",
							 "model",         "",            DATA_STRING, "TX29 Sensor",
							 "id",            "",            DATA_INT,    sensor_id,
							 "battery",       "Battery",     DATA_STRING, battery_low ? "LOW" : "OK",
							 "newbattery",    "NewBattery",  DATA_INT,	  newbatt,
							 "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
							 "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
							 "crc",           "CRC",         DATA_STRING, "OK",
							 NULL);
		
		data_acquired_handler(data);
		events++;
	}
	return events;
}

static char *output_fields[] = {
    "time",
	"brand",
    "model",
    "id",
	"battery",
	"newbattery",
	"status",
    "temperature_C",
    "humidity",
	"crc",
    NULL
};

r_device lacrosse_tx35 = {
    .name           = "LaCrosse TX35 Temperature / Humidity Sensor",
	.modulation     = FSK_PULSE_PCM,
	//.short_limit    = 55,
	//.long_limit     = 55,
	.short_limit    = 55,
	.long_limit     = 55,
	.reset_limit    = 5000,
	.json_callback  = &lacrossetx35_callback,
	.disabled       = 0,
	.demod_arg      = 0,
	.fields         = output_fields,
};
