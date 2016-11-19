/*
 * Kangtai Switches (Commonly used in Cotech devices from Clas Ohlson)
 *
 * Copyright (c) 2016, Mark Olsson <mark.olsson@telldus.se>
 *
 * Devices tested:
 * - Cotech 4 channel remote control 36-6361
 * - Messages from TellStick Net sending this protocol
 *
 * Protocol:
 * Start bit = 375us high, 2250us low
 * followed by 24 bits of data
 * One = 1125us high, 375us low
 * Zero = 375us high, 1125us low
 *
 * Data layout:
 * 16 bit address of transmitter
 * 2 bit rolling code (00, 01, 10, 11)
 * 1 bit device command (1 = On, 2 = Off)
 * 5 bit unit address (00000 = group, 11111 = factory use only)
 * Data is 'decrypted' with a lookup table
 *
 */

#include "rtl_433.h"
#include "util.h"

static int kangtai_callback(bitbuffer_t *bitbuffer) {
	uint8_t *bytes = bitbuffer->bb[0];
	data_t *data;
	uint8_t a0, a1, a2, a3, a4, a5;
	uint8_t b0, b1, b2, b3, b4, b5;
	uint16_t address;

	// Get the current time
	char time_str[LOCAL_TIME_BUFLEN];
	local_time_str(0, time_str);

	/* Decryption lookup table */
	char table1[] = { 1, 8, 4, 14, 2, 7, 13, 6, 15, 12, 0, 10, 3, 11, 5, 9 };
	char table2[] = { 15, 6, 0, 11, 5, 2, 10, 4, 12, 13, 14, 8, 1, 9, 3, 7 };

	/* Check for 24 bits of data, then invert each bit */
	if (24 != bitbuffer->bits_per_row[0]) return 0;
	bitbuffer_invert(bitbuffer);

	/* Decrypt data */
	/* Split data into 4 bit groups */
	a5 = (bytes[0] >> 4) & 0x0F;
	a4 = bytes[0] & 0x0F;
	a3 = (bytes[1] >> 4) & 0x0F;
	a2 = bytes[1] & 0x0F;
	a1 = (bytes[2] >> 4) & 0x0F;
	a0 = bytes[2] & 0x0F;

	/* If bit 21 = 0, use lookup table 1, else table 2 */
	if ((a5 & 2) == 0) {
		b5 = a5 ^ 9;
		b4 = table1[a4] ^ a3;
		b3 = table1[a3] ^ a2;
		b2 = table1[a2] ^ a1;
		b1 = table1[a1] ^ a0;
		b0 = table1[a0];
	} else {
		b5 = a5 ^ 9;
		b4 = table2[a4] ^ a3;
		b3 = table2[a3] ^ a2;
		b2 = table2[a2] ^ a1;
		b1 = table2[a1] ^ a0;
		b0 = table2[a0];
	}

	/* Assign decrypted data back to byte array */
	bytes[0] = (b5 << 4) | b4;
	bytes[1] = (b3 << 4) | b2;
	bytes[2] = (b1 << 4) | b0;

	// Assign first 16 bits to the device address
	address = (bytes[0] << 8) | bytes[1];

	data = data_make(
		"time",    "Time",    DATA_STRING, time_str,
		"model",   "Model",   DATA_STRING, "Kangtai",
		"address", "Address", DATA_FORMAT, "%x", DATA_INT, ((bytes[0] << 8) | bytes[1]),
		"loop",    "Loop",    DATA_FORMAT, "%d", DATA_INT, ((bytes[2] >> 6) & 3),
		"command", "Command", DATA_STRING, ((bytes[2] >> 5) & 0x01) ? "on" : "off",
		"unit",    "Unit",    DATA_FORMAT, "%d", DATA_INT, (bytes[2] & 0x1F),
		NULL
	);
	data_acquired_handler(data);
	return 1;
}

static char *output_fields[] = {
	"time",
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
