
#include "rtl_433.h"
#include "util.h"
#include "data.h"

#define LACROSSE_TX29_BITLENGH         65 // Message is 64 bits but rtl_433 detects 65
#define LACROSSE_TX29_STARTBYTE1     0xaa
#define LACROSSE_TX29_STARTBYTE2     0x2d
#define LACROSSE_TX29_STARTBYTE3     0xd4
#define LACROSSE_TX29_DATALENGH         9 // length, in nibbles, of usefull data
#define LACROSSE_TX29_NOHUMIDSENSOR  0x6a // Sensor do not support humidty
#define LACROSSE_TX29_CRC_POLY       0x31
#define LACROSSE_TX29_CRC_INIT       0x00

// LaCrosse TX-6u, TX-7u,  Temperature and Humidity Sensors
// Temperature and Humidity are sent in different messages bursts.
static int lacrossetx35_callback(bitbuffer_t *bitbuffer) {
	char time_str[LOCAL_TIME_BUFLEN];
	uint8_t *bb;
    uint16_t brow, row_nbytes;
	uint8_t msg_type, r_crc, c_crc;
	int valid = 0;
	data_t *data;
	int events = 0;	
	
	local_time_str(0, time_str);
	for (brow = 0; brow < bitbuffer->num_rows; ++brow) {
		bb = bitbuffer->bb[brow];
		// Validate message and reject it as fast as possible
		if (bitbuffer->bits_per_row[brow] != LACROSSE_TX29_BITLENGH) 
			continue;
		// Detect preamble : 0xaa (10101010) followed by a probably brand identifier (0x2d 0xd4)
		if (bb[0] != LACROSSE_TX29_STARTBYTE1 
			&& bb[1] != LACROSSE_TX29_STARTBYTE2
			&& bb[2] != LACROSSE_TX29_STARTBYTE3) {
			continue;
		}
		if (debug_output >= 1)
			fprintf(stderr, "LaCrosse TX29 detected\n");
		row_nbytes = (bitbuffer->bits_per_row[brow] + 7)/8;
		/*
		 * Check message integrity (CRC/Checksum/parity)
		 * Normally, it is computed on the whole message, from byte 0 (preamble) to byte 6,
		 * but preamble is always the same, so we can speed the process by doing a crc check
		 * only on byte 3,4,5,6
		 */
		int datalength = (bb[3] >> 4);
		if ( datalength != LACROSSE_TX29_DATALENGH) {
			if (debug_output >= 1)
				fprintf(stderr, "%s LaCrosseTX35 bad data length: expected %d, received %d\n",
					time_str, LACROSSE_TX29_DATALENGH, datalength);
			// reject row
			continue;
		}
		r_crc = bb[7];
		c_crc = crc8(&bb[3], 4, LACROSSE_TX29_CRC_POLY, LACROSSE_TX29_CRC_INIT);
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
		/*
		
		int sensid;
  int newbatt;
  float temp;
  float rel_hum;
  int weakbatt;
  if (len == 5) {
    // the tm is correct
    sensid = ((tm[3] & 0x0f) << 2) | ((tm[4]>>6) & 0x03);
    newbatt = (tm[4] >> 5) & 1;
    temp = 10.0 * (tm[4] & 0x0F) +  1.0 *((tm[5]>>4) & 0x0F) + 0.1 * (tm[5] & 0x0F) - 40.0;
    weakbatt = (tm[6]>>7) & 1;
    rel_hum = 1.0 * (tm[6] & 0x7F);
  } else {
    logging_warning( "Don't know how to handle data of length %i.\n", len );
    return -5;
  }
  **/	
		uint8_t sensor_id, newbatt, battery_low;
		uint8_t humidity; // in %. If > 100 : no humidity sensor
		float temp_c; // in °C
		sensor_id = ((bb[3] & 0x0f) << 2) | ((bb[4]>>6) & 0x03);
		temp_c = 10.0 * (bb[4] & 0x0f) +  1.0 *((bb[5]>>4) & 0x0f) + 0.1 * (bb[5] & 0x0f) - 40.0;
		newbatt = (bb[4] >> 5) & 1;
		battery_low = (bb[6]>>7) & 1;
		humidity = 1 * (bb[6] & 0x7F);
		if (humidity == LACROSSE_TX29_NOHUMIDSENSOR) 
			humidity = -1;
		//if (humidity == LACROSSE_TX29_NOHUMIDSENSOR) {
			/**data = data_make("time",          "",            DATA_STRING, time_str,
							 "model",         "",            DATA_STRING, "LaCrosse TX29 Sensor",
							 "id",            "",            DATA_INT, sensor_id,
							 "battery",       "Battery",     DATA_STRING, battery_low ? "LOW" : "OK",
							 "newbattery"     "NewBattery",  DATA_STRING, newbatt ? "YES" : "NO",
							 "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
							 NULL);**/
		//} else {		
			data = data_make("time",          "",            DATA_STRING, time_str,
							 "brand",         "",            DATA_STRING, "LaCrosse",
							 "model",         "",            DATA_STRING, "TX29 Sensor",
							 "id",            "",            DATA_INT,    sensor_id,
							 "battery",       "Battery",     DATA_STRING, battery_low ? "LOW" : "OK",
							 "newabttery",    "NewBattery",  DATA_INT,	  newbatt,
							 "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
							 "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
							 "crc",           "CRC",         DATA_STRING,    "ok",
							 NULL);
		//}
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
	"newabttery",
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
	.short_limit    = 58,
	.long_limit     = 58,
	.reset_limit    = 5000,
	.json_callback  = &lacrossetx35_callback,
	.disabled       = 0,
	.demod_arg      = 0,
	.fields         = output_fields,
};
