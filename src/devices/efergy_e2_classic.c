/* Efergy e2 classic (electricity meter)
 *
 * This electricity meter periodically reports current power consumption
 * on frequency ~433.55 MHz. The data that is transmitted consists of 8
 * bytes:
 *
 * Byte 1-4: Start bits (0000), then static data (probably device id)
 * Byte 5-7: Current power consumption
 * Byte 8: Checksum
 *
 * Power calculations come from Nathaniel Elijah's program EfergyRPI_001.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"

static int efergy_e2_classic_callback(bitbuffer_t *bitbuffer) {
	unsigned num_bits = bitbuffer->bits_per_row[0];
	uint8_t *bytes = bitbuffer->bb[0];

	if (num_bits < 64 || num_bits > 80) {
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
		for (unsigned i = 0; i < 8; ++i) {
			bytes[i] = ~bytes[i];
		}
	}

	unsigned checksum = 0;
	for (unsigned i = 0; i < 7; ++i) {
		checksum += bytes[i];
	}
	checksum &= 0xff;
	if (checksum != bytes[7]) {
		return 0;
	}

	const unsigned VOLTAGES[] = {110, 115, 120, 220, 230, 240, 0};

	double current_adc = 256 * bytes[4] + bytes[5];
	for (unsigned i = 0; VOLTAGES[i] != 0; ++i) {
		double power  = (VOLTAGES[i] * current_adc * (1 << bytes[6])) / 32768;
		fprintf(stderr, "Power consumption at %u volts: %.2f watts\n", VOLTAGES[i], power);
	}

	return 1;
}


r_device efergy_e2_classic = {
	.name           = "Efergy e2 classic",
	.modulation     = FSK_PULSE_PWM_RAW,
	.short_limit    = 92,
	.long_limit     = 400,
	.reset_limit    = 400,
	.json_callback  = &efergy_e2_classic_callback,
	.disabled       = 1,
	.demod_arg      = 0,
};
