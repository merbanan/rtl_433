/* Protocol of the SimpliSafe Sensors
 *
 * The data is sent leveraging a PiWM Encoding where a long is 1, and a short is 0
 *
 * All bytes are sent with least significant bit FIRST (1000 0111 = 0xE1)
 *
 *  2 Bytes   | 1 Byte       | 5 Bytes   | 1 Byte  | 1 Byte  | 1 Byte	    | 1 Byte
 *  Sync Word | Message Type | Device ID | CS Seed | Command | SUM CMD + CS | Epilogue
 *
 * Copyright (C) 2018 Adam Callis <adam.callis@gmail.com>
 * License: GPL v2+ (or at your choice, any other OSI-approved Open Source license)
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "data.h"
#include "util.h"

static void
ss_get_id(char *id, uint8_t *b)
{
	char *p = id;

	// Change to least-significant-bit last (protocol uses least-siginificant-bit first) for hex representation:
	for (uint16_t k = 3; k <= 7; k++) {
		b[k] = reverse8(b[k]);
		sprintf(p++, "%c", (char)b[k]);
	}
	*p = '\0';
}

static int
ss_sensor_parser (bitbuffer_t *bitbuffer)
{
	char time_str[LOCAL_TIME_BUFLEN];
	data_t *data;
	uint8_t *b = bitbuffer->bb[0];
	char id[6];
	char extradata[30] = "";

	// each row needs to have exactly 92 bits 
	if (bitbuffer->bits_per_row[0] != 92)
		return 0;

	ss_get_id(id, b);

	if (b[9] == 64) {
		strcpy(extradata,"Contact Closed");
	} else if (b[9] == 128) {
		strcpy(extradata,"Contact Open");
	} else if (b[9] == 192) {
		strcpy(extradata,"Alarm Off");
	}

	local_time_str(0, time_str);
	data = data_make(
		"time",		"",	DATA_STRING, time_str,
		"model",	"",	DATA_STRING, "SimpliSafe Sensor",
		"device",	"Device ID",	DATA_STRING, id,
		"seq",		"Sequence",	DATA_INT, b[8],
		"state",	"State",	DATA_INT, b[9],
		"extradata",	"Extra Data",	DATA_STRING, extradata,
		NULL
	);
	data_acquired_handler(data);

	return 1;
}

static int 
ss_pinentry_parser(bitbuffer_t *bitbuffer)
{
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    uint8_t *b = bitbuffer->bb[0];
    char id[6];
    char extradata[30];
    // In a keypad message the pin is encoded in bytes 10 and 11 with the the digits each using 4 bits
    // However the bits are low order to high order
    int digits[5];
    int pina = reverse8(b[10]);
    int pinb = reverse8(b[11]);

    digits[0] = (pina & 0xf);
    digits[1] = ((pina & 0xf0) >> 4);
    digits[2] = (pinb & 0xf);
    digits[3] = ((pinb & 0xf0) >> 4);

    ss_get_id(id, b);

    sprintf(extradata, "Disarm Pin: %x%x%x%x", digits[0], digits[1], digits[2], digits[3]);

	local_time_str(0, time_str);
	data = data_make(
		"time",		"",	DATA_STRING, time_str,
		"model",	"",	DATA_STRING, "SimpliSafe Keypad",
		"device",	"Device ID",	DATA_STRING, id,
		"seq",		"Sequence",	DATA_INT, b[9],
		"extradata",	"Extra Data",	DATA_STRING, extradata,
		NULL
	);
	data_acquired_handler(data);

	return 1;
}

static int 
ss_keypad_commands (bitbuffer_t *bitbuffer)
{
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    uint8_t *b = bitbuffer->bb[0];
    char id[6];
    char extradata[30]; // = "Arming: ";

    if (b[10] == 0x6a) {
        strcpy(extradata, "Arm System - Away");
	} else if (b[10] == 0xca) {
		strcpy(extradata, "Arm System - Home");
	} else if (b[10] == 0x3a) {
		strcpy(extradata, "Arm System - Cancelled");
	} else if (b[10] == 0x2a) {
		strcpy(extradata, "Keypad Panic Button");
	} else if (b[10] == 0x86) {
		strcpy(extradata, "Keypad Menu Button");
	} else {
		sprintf(extradata, "Unknown Keypad: %02x", b[10]);
	}

	ss_get_id(id, b);

	local_time_str(0, time_str);
	data = data_make(
		"time",		"",	DATA_STRING, time_str,
		"model",	"",	DATA_STRING, "SimpliSafe Keypad",
		"device",	"",	DATA_STRING, id,
		"seq",	"Sequence",DATA_INT, b[9],
		"extradata",	"",	DATA_STRING, extradata,
		NULL
	);
	data_acquired_handler(data);

	return 1;
}

static int
ss_sensor_callback (bitbuffer_t *bitbuffer)
{
	bitbuffer_invert(bitbuffer); // Invert the Bits
	uint8_t *b = bitbuffer->bb[0];

	if (b[0] != 0x33 && b[1] != 0xa0) // All Messages Must start with 0x33a0
		return 0;

	if (b[2] == 0x88) {
		return ss_sensor_parser (bitbuffer);
	} else if (b[2] == 0x66) {
		return ss_pinentry_parser (bitbuffer);
	} else if (b[2] == 0x44) {
		return ss_keypad_commands (bitbuffer);
	} else {
		if (debug_output)
			fprintf(stderr, "Unknown Message Type: %02x\n", b[2]);
		return 0;
	}
}

static char *sensor_output_fields[] = {
    "time",
    "model",
    "device",
    "seq",
    "state",
    "extradata",
    NULL
};

r_device ss_sensor = {
    .name           = "SimpliSafe Home Security System (May require disabling automatic gain for KeyPad decodes)",
    .modulation     = OOK_PULSE_PIWM_DC,
    .short_limit    = 500,  // half-bit width 500 us
    .long_limit     = 1000, // bit width 1000 us
    .reset_limit    = 1500,
    .tolerance      = 100, // us
    .json_callback  = &ss_sensor_callback,
    .disabled       = 1,
    .fields         = sensor_output_fields
};
