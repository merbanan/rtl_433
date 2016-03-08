/* Efergy Optical (electricity meter)
 *
 * This electricity meter periodically reports current power consumption
 * on frequency ~433.55 MHz. The data that is transmitted consists of 8
 * bytes:
 *
 * Byte 1-4: Start bits (0000), then static data (probably device id)
 * Byte 5-7: all zeros 
 * Byte 8: Pulse Count
 * Byte 9: Unknown (value is 15 but 16 for low pulses)
 * Byte 10: seconds
 * Byte 11: crc16 xmodem
 * Byte 12: crc16 xmodem
 * if pulse count <3 then energy =(( pulsecount/impulse-perkwh) * (3600/seconds))
 * else energy = ((pulsecount/n_imp) * (3600/seconds))
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "util.h"
#include "data.h"

static int efergy_optical_callback(bitbuffer_t *bitbuffer) {
	unsigned num_bits = bitbuffer->bits_per_row[0];
	uint8_t *bytes = bitbuffer->bb[0];
	float energy;
	int pulsecount, n_imp;
	int seconds;
	data_t *data;
        char time_str[LOCAL_TIME_BUFLEN];

	if (num_bits < 64 || num_bits > 100) {
		return 0;
	}

	// The bit buffer isn't always aligned to the transmitted data, so
	// search for data start and shift out the bits which aren't part
	// of the data. The data always starts with 0000 (or 1111 if
	// gaps/pulses are mixed up).
	while ((bytes[0] & 0xf0) != 0xf0 && (bytes[0] & 0xf0) != 0x00) {
		num_bits -= 1;
		if (num_bits < 64) {
			return 0;
		}

		for (unsigned i = 0; i < (num_bits + 7) / 8; ++i) {
			bytes[i] <<= 1;
			bytes[i] |= (bytes[i + 1] & 0x80) >> 7;
		}
	}

	// Sometimes pulses and gaps are mixed up. If this happens, invert
	// all bytes to get correct interpretation.
	if (bytes[0] & 0xf0) {
		for (unsigned i = 0; i < 11; ++i) {
			bytes[i] = ~bytes[i];
		}
	}

	n_imp = 3200;
        printf ("\nEfergy Optical\nPulse per KWH default is set to %2d \n", n_imp );

	pulsecount =  bytes[8];
	seconds = bytes[10];

       //some crazy logic for low pulse count not sure how I reached this formula
	if (pulsecount < 3)
	{
	energy = ((pulsecount/n_imp) * (3600/seconds));
        }
         else
         {
         energy = ((pulsecount/n_imp) * (3600/30));
         }
 
        /* Get time now */
        local_time_str(0, time_str);

	 data = data_make("time",          "",            DATA_STRING, time_str,
        	          "model",         "",            DATA_STRING, "Efergy Optical",
                	  "energy",       "Energy KWh",     DATA_FORMAT,"%.03f KWh", DATA_DATA, energy,
	                  NULL);
	 data_acquired_handler(data);
	return 1;
	}

	static char *output_fields[] = {
	    "time",
	    "model",
	    "energy",
	    NULL
	};



r_device efergy_optical = {
	.name           = "Efergy Optical",
	.modulation     = FSK_PULSE_PWM_RAW,
	.short_limit    = 92,
	.long_limit     = 400,
	.reset_limit    = 400,
	.json_callback  = &efergy_optical_callback,
	.disabled       = 0,
	.demod_arg      = 0,
};
