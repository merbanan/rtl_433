/*
 * LaCrosse/StarMétéo/Conrad TX35DTH-IT, TX29-IT,  Temperature and Humidity Sensors.
 * Tune to 868240000Hz
 *
*
Protocol
========
Example Data (gfile-tx29.data : https://github.com/merbanan/rtl_433_tests/tree/master/tests/lacrosse/06)
   a    a    2    d    d    4    9    2    8    4    4    8    6    a    e    c
Bits :
1010 1010 0010 1101 1101 0100 1001 0010 1000 0100 0100 1000 0110 1010 1110 1100
Bytes num :
----1---- ----2---- ----3---- ----4---- ----5---- ----6---- ----7---- ----8----
~~~~~~~~~ 1st byte
preamble, always "0xaa"
          ~~~~~~~~~~~~~~~~~~~ bytes 2 and 3
brand identifier, always 0x2dd4
                              ~~~~ 1st nibble of bytes 4
datalength (always 9) in nibble, including this field and crc
                                   ~~~~ ~~ 2nd nibble of bytes 4 and 1st and 2nd bits of byte 5
Random device id (6 bits)
                                          ~ 3rd bits of byte 5
new battery indicator
                                           ~ 4th bits of byte 5
unkown, unused
                                             ~~~~ ~~~~ ~~~~ 2nd nibble of byte 5 and byte 6
temperature, in bcd *10 +40
                                                            ~ 1st bit of byte 7
weak battery
                                                             ~~~ ~~~~ 2-8 bits of byte 7
humidity, in%. If == 0x6a : no humidity sensor
                                                                      ~~~~ ~~~~ byte 8
crc8 of bytes


Developer's comments
====================
I have noticed that depending of the device, the message received has different length.
It seems some sensor send a long preamble (33 bits, 0 / 1 alternated), and some send only
one byte as the preamble. I own 3 sensors TX29, and two of them send a long preamble.
So this decoder synchronize on the 0xaa 0x2d 0xd4 preamble, so many 0xaa can occurs before.
Also, I added 0x9 in the preamble (the weather data length), because this decoder only handle
this type of message.
TX29 and TX35 share the same protocol, but pulse are different length, thus this decoder
handle the two signal and we use two r_device struct (only differing by the pulse width)

How to make a decoder : https://enavarro.me/ajouter-un-decodeur-ask-a-rtl_433.html
*/

#include "rtl_433.h"
#include "util.h"
#include "data.h"

#define LACROSSE_TX29_NOHUMIDSENSOR  0x6a // Sensor do not support humidty
#define LACROSSE_TX35_CRC_POLY       0x31
#define LACROSSE_TX35_CRC_INIT       0x00
#define LACROSSE_TX29_MODEL          29 // Model number
#define LACROSSE_TX35_MODEL          35

/**
 ** Generic decoder for LaCrosse "IT+" (instant transmission) protocol
 ** Param device29or35 contain "29" or "35" depending of the device.
 **/
static int lacrosse_it(bitbuffer_t *bitbuffer, uint8_t device29or35) {
	char time_str[LOCAL_TIME_BUFLEN];
	uint8_t *bb;
	uint16_t brow, row_nbytes;
	uint8_t msg_type, r_crc, c_crc;
	uint8_t sensor_id, newbatt, battery_low;
	uint8_t humidity; // in %. If > 100 : no humidity sensor
	float temp_c; // in °C
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
			fprintf(stderr, "LaCrosse TX29/35 detected, buffer is %d bits length, device is TX%d\n", bitbuffer->bits_per_row[brow], device29or35);
		// remove preamble and keep only 64 bits
		bitbuffer_extract_bytes(bitbuffer, brow, start_pos, out, 64);

		/*
		 * Check message integrity (CRC/Checksum/parity)
		 * Normally, it is computed on the whole message, from byte 0 (preamble) to byte 6,
		 * but preamble is always the same, so we can speed the process by doing a crc check
		 * only on byte 3,4,5,6
		 */
		r_crc = out[7];
		c_crc = crc8(&out[3], 4, LACROSSE_TX35_CRC_POLY, LACROSSE_TX35_CRC_INIT);
		if (r_crc != c_crc) {
			// example debugging output
			if (debug_output >= 1)
				fprintf(stderr, "%s LaCrosse TX29/35 bad CRC: calculated %02x, received %02x\n",
					time_str, c_crc, r_crc);
			// reject row
			continue;
		}

		/*
		 * Now that message "envelope" has been validated,
		 * start parsing data.
		 */
		sensor_id = ((out[3] & 0x0f) << 2) | ((out[4]>>6) & 0x03);
		temp_c = 10.0 * (out[4] & 0x0f) +  1.0 *((out[5]>>4) & 0x0f) + 0.1 * (out[5] & 0x0f) - 40.0;
		newbatt = (out[4] >> 5) & 1;
		battery_low = (out[6]>>7) & 1;
		humidity = 1 * (out[6] & 0x7F); // Bit 1 to 7 of byte 6
		if (humidity == LACROSSE_TX29_NOHUMIDSENSOR) {
			data = data_make("time",		  "",			DATA_STRING, time_str,
							 "brand",		 "",			DATA_STRING, "LaCrosse",
							 "model",		 "",			DATA_STRING, (device29or35 == 29 ? "TX29-IT" : "TX35DTH-IT"),
							 "id",			"",			DATA_INT,	sensor_id,
							 "battery",	   "Battery",	 DATA_STRING, battery_low ? "LOW" : "OK",
							 "newbattery",	"NewBattery",  DATA_INT,	  newbatt,
							 "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
							 "mic",		   "Integrity",		 DATA_STRING, "CRC",
							 NULL);
		}
		else {
			data = data_make("time",		  "",			DATA_STRING, time_str,
							"brand",		 "",			DATA_STRING, "LaCrosse",
							"model",		 "",			DATA_STRING, (device29or35 == 29 ? "TX29-IT" : "TX35DTH-IT"),
							"id",			"",			DATA_INT,	sensor_id,
							"battery",	   "Battery",	 DATA_STRING, battery_low ? "LOW" : "OK",
							"newbattery",	"NewBattery",  DATA_INT,	  newbatt,
							"temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
							"humidity",	  "Humidity",	DATA_FORMAT, "%u %%", DATA_INT, humidity,
							"mic",		   "Integrity",		 DATA_STRING, "CRC",
							NULL);
		}
		// humidity = -1; // The TX29-IT sensor do not have humidity. It is replaced by a special value


		data_acquired_handler(data);
		events++;
	}
	return events;
}

/**
 ** Wrapper for the TX29 device
 **/
static int lacrossetx29_callback(bitbuffer_t *bitbuffer) {
	return lacrosse_it(bitbuffer, LACROSSE_TX29_MODEL);
}

/**
 ** Wrapper for the TX35 device
 **/
static int lacrossetx35_callback(bitbuffer_t *bitbuffer) {
	return lacrosse_it(bitbuffer, LACROSSE_TX35_MODEL);
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
	"mic",
	NULL
};

// Receiver for the TX29 device
r_device lacrosse_tx29 = {
	.name           = "LaCrosse TX29IT Temperature sensor",
	.modulation     = FSK_PULSE_PCM,
	.short_limit    = 55,
	.long_limit     = 55,
	.reset_limit    = 4000,
	.json_callback  = &lacrossetx29_callback,
	.disabled       = 0,
	.demod_arg      = 0,
	.fields         = output_fields,
};

// Receiver for the TX35 device
r_device lacrosse_tx35 = {
	.name           = "LaCrosse TX35DTH-IT Temperature sensor",
	.modulation     = FSK_PULSE_PCM,
	.short_limit    = 105,
	.long_limit     = 105,
	.reset_limit    = 4000,
	.json_callback  = &lacrossetx35_callback,
	.disabled       = 0,
	.demod_arg      = 0,
	.fields         = output_fields,
};
