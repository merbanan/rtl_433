/** @file
    Decoder for Rolling Code Transmitter

    Copyright (C) 2020 David E. Tiller

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
 The device uses OOK_PULSE_PCM_RZ encoding.
 The packet starts with either a narrow (500 uS) start pulse or a long (1500 uS) pulse.
 - 0 is defined as a 1500 uS gap followed by a  500 uS pulse.
 - 1 is defined as a 1000 uS gap followed by a 1000 uS pulse.
 - 2 is defined as a  500 uS gap followed by a 1500 uS pulse.

 Transmissions consist of a '1' length packet of 20 'trits' (trinary digits)
 followed by a '3' length packet of 20 trits. These two packets are repeated
 some number of times.
 The trits represent a rolling code that changes on each keypress, a fixed
 16 trit device ID,  3 id trits (key pressed), and a 1 trit button id.
 All of the data is obfuscated and a 1 length and a 3 length packet are
 required to successfully decode a transmission.
 */

#include <stdlib.h>
#include "decoder.h"

#define MIN_BITS		80
#define TRINARY_SIZE		20
#define RAW_SIZE		10
#define SPECIAL_BITS		4
#define DEV_ID_SIZE		(RAW_SIZE * 2 - SPECIAL_BITS)
#define NOT_SET			'.'
#define BUTTON_TRIT		9
#define ID_TBIT_START		6

static int get_start_bit_width(uint8_t *buffer, int *index, int num_bits, int debug_output) {
    int start = 0;

    while (*index < num_bits && bitrow_get_bit(buffer, *index) == 0) {
        (*index)++;
    }

    if (*index == num_bits) {
        if (debug_output > 1) {
            fprintf(stderr, "No 1 bit in row\n");
        }

        return -1;
    }

    start = (*index)++;

    while (*index < num_bits && bitrow_get_bit(buffer, *index) == 1) {
        (*index)++;
    }

    if (*index == num_bits) {
        if (debug_output > 1) {
            fprintf(stderr, "No 0 bit after start bit in row\n");
        }

        return -1;
    }

    return *index - start;
}

static uint8_t get_trits(uint8_t *nibble_buffer, uint8_t *bit_buffer, int *bit_index, int num_bits, int debug_output) {
    if (num_bits - *bit_index < MIN_BITS) {
        if (debug_output > 1) {
            fprintf(stderr, "Too few bits: %d\n", num_bits - *bit_index);
	}

	return 1;
    }

    int nibble_index = 0;
   while (nibble_index < TRINARY_SIZE && *bit_index <= num_bits - 4) {
        uint8_t nibble = bitrow_get_bit(bit_buffer, (*bit_index)++) << 3 | bitrow_get_bit(bit_buffer, (*bit_index)++) << 2 | 
            bitrow_get_bit(bit_buffer, (*bit_index)++) << 1 | bitrow_get_bit(bit_buffer, (*bit_index)++);
        
	if (nibble == 0x01) nibble_buffer[nibble_index++] = 0;
	else if (nibble == 0x03) nibble_buffer[nibble_index++] = 1;
	else if (nibble == 0x07) nibble_buffer[nibble_index++] = 2;
	else {
		fprintf(stderr, "Unknown nibble %02x\n", nibble);
		return 1;
	}
    }

    if (nibble_index != TRINARY_SIZE) {
        fprintf(stderr, "Not enough bits for %d nibbles\n", TRINARY_SIZE);

        return 1;
    } else {
        return 0;
    }
}

static void fix_a_code(uint8_t *a_raw, uint8_t *r_raw, uint8_t *dst) {
    uint8_t prev = 0;
    for (int i = 0; i < RAW_SIZE; i++) {
        int16_t x = a_raw[i] - r_raw[i];
	if (x < 0) x += 3;
	x = x - prev;
	if (x < 0) x += 3;
	dst[i] = x & 0xff;
	prev = a_raw[i];
    }
}

// Buffer has to be len + 1 bytes long.
static void raw_to_chars(uint8_t *src, char *dst, int len) {
	for (int i = 0; i < len; i++) {
		dst[i] = src[i] + '0';
	}

	dst[len] = '\0';
}

static uint32_t raw_to_uint(uint8_t *ptr, int len) {
    uint32_t value = 0;
    
    for (int i = 0; i < len; i++) {
        value *= 3;
        value += ptr[i];
    }

    return value;
}

static void rotate_mirror_add(uint32_t *counter, uint32_t *mirror, int c) {
    uint32_t result = 0;
    for (int i = 0; i < c; i++) {
        uint8_t msb = *mirror >> 31;
        *mirror <<= 1;
        *mirror += msb;
    }

    *counter += *mirror;
}

static void mirror_counter(uint32_t *counter, uint32_t *mirror) {
    uint32_t result = 0;
    for (int i = 0; i < 32; i++) {
        uint8_t bit = *counter & 0x01;
        *counter >>= 1;
        result <<= 1;
        result |= bit;
    }

    *mirror = result;
}

static uint32_t get_rolling_code(uint32_t r1, uint32_t r3) {
    uint32_t counter = r3;
    uint32_t mirror = r1;

    counter += mirror;

    rotate_mirror_add(&counter, &mirror, 3);
    rotate_mirror_add(&counter, &mirror, 2);
    rotate_mirror_add(&counter, &mirror, 2);
    rotate_mirror_add(&counter, &mirror, 2);
    rotate_mirror_add(&counter, &mirror, 1);
    rotate_mirror_add(&counter, &mirror, 3);
    rotate_mirror_add(&counter, &mirror, 1);
    rotate_mirror_add(&counter, &mirror, 1);

    mirror_counter(&counter, &mirror);

    return mirror;
}

static int rolling_code_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // This holds previous start width 1 fixed and rolling codes for 
    // counter decoding once we have a start width 3 packet.
    static uint8_t prev_1_a_corrected[RAW_SIZE] = {NOT_SET};
    static uint8_t prev_1_r_raw[RAW_SIZE] = {NOT_SET};

    data_t *data = NULL;
    int index = 0; // a row index
    uint8_t *b = NULL; // bits of a row
    int num_bits = 0;
    int start_width = 0;
    uint8_t trinary[TRINARY_SIZE] = {0};
    int debug_output = decoder->verbose;

    if (debug_output > 1) {
    	bitbuffer_printf(bitbuffer, "%s: ", __func__);
    }

    if (bitbuffer->num_rows < 1) {
	*prev_1_a_corrected = NOT_SET;
	*prev_1_r_raw = NOT_SET;

        return 0;
    }

    b = bitbuffer->bb[0];
    num_bits = bitbuffer->bits_per_row[0];

    start_width = get_start_bit_width(b, &index, num_bits, debug_output);

    if (start_width != 1 && start_width != 3) {
        if (debug_output > 1) {
            fprintf(stderr, "Start bit width invalid: %d\n", start_width);
        }

	*prev_1_a_corrected = NOT_SET;
	*prev_1_r_raw = NOT_SET;

        return 0;
    }

    /*
     * Now that message "envelope" has been validated,
     * start parsing data.
     */

     if (get_trits(trinary, b, &index, num_bits, debug_output)) {
        fprintf(stderr, "get_trits failed\n");
	*prev_1_a_corrected = NOT_SET;
	*prev_1_r_raw = NOT_SET;

        return 0;
    }
    
    if (debug_output > 1) {
        char buffer[TRINARY_SIZE + 1];
        raw_to_chars(trinary, buffer, TRINARY_SIZE);
        data = data_append(data, 
		"raw_trinary", "", DATA_STRING, buffer,
        	"start_width", "", DATA_INT, start_width, NULL);
    }

    // Tease out the individual parts of the message

    uint8_t a_raw[RAW_SIZE] = {0};
    uint8_t a_corrected[RAW_SIZE] = {0};
    uint8_t r_raw[RAW_SIZE] = {0};

    // Pull out A and R bits
    for (int i = 0; i < RAW_SIZE; i++) {
	a_raw[i] = trinary[i * 2 + 1];
	r_raw[i] = trinary[i * 2];
    }       

    fix_a_code(a_raw, r_raw, a_corrected);

    if (debug_output > 1) {
	char buffer[RAW_SIZE + 1];
	raw_to_chars(a_raw, buffer, RAW_SIZE);
        data = data_append(data, "raw_a", "", DATA_STRING, buffer, NULL);
	raw_to_chars(r_raw, buffer, RAW_SIZE);
        data = data_append(data, "raw_r", "", DATA_STRING, buffer, NULL);
	raw_to_chars(a_corrected, buffer, RAW_SIZE);
        data = data_append(data, "corrected_a", "", DATA_STRING, buffer, NULL);
    }

    if (start_width == 1) {
	memcpy(prev_1_a_corrected, a_corrected, RAW_SIZE);
	memcpy(prev_1_r_raw, r_raw, RAW_SIZE);
    }

    if (*prev_1_r_raw != NOT_SET && start_width == 3) {
	char buffer[RAW_SIZE + 1];
	uint32_t counter = get_rolling_code(raw_to_uint(prev_1_r_raw, RAW_SIZE), raw_to_uint(r_raw, RAW_SIZE));
    	sprintf(buffer, "%u", counter);
	data = data_append(data, 
		"counter", "", DATA_STRING, buffer, NULL);
	sprintf(buffer, "%08x", counter);
	data = data_append(data, 
		"counter_hex", "", DATA_STRING, buffer,
        	"button_pressed", "", DATA_INT, (int) a_corrected[BUTTON_TRIT],
        	"id_bits", "", DATA_INT, a_corrected[ID_TBIT_START] * 9 + a_corrected[ID_TBIT_START + 1] * 3 + a_corrected[ID_TBIT_START + 2], NULL);

	uint8_t device_id[DEV_ID_SIZE];
	memset(device_id, 0, DEV_ID_SIZE);
	memcpy(device_id + RAW_SIZE, a_corrected, RAW_SIZE - SPECIAL_BITS);
	
	memcpy(device_id, prev_1_a_corrected, RAW_SIZE);
	uint32_t value = raw_to_uint(device_id, DEV_ID_SIZE);
	sprintf(buffer, "%08x", value);
	data = data_append(data, 
		"device_id", "", DATA_INT, value,
		"device_id_hex", "", DATA_STRING, buffer, NULL);
    }

    if (data != NULL) {
	data = data_prepend(data, "model", "", DATA_STRING, "Rolling Code Transmitter", NULL);
    	decoder_output_data(decoder, data);
    }

    // Return 1 if message successfully decoded
    return 1;
}

/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */
static char *output_fields[] = {
	"model",
        "device_id",
        "device_id_hex",
        "counter",
	"counter_hex",
	"id_bits",
	"button_pressed",
        NULL,
};

r_device rolling_code = {
        .name        = "Rolling Code Transmitter (-f 315M)",
        .modulation  = OOK_PULSE_PCM_RZ,
        .short_width = 500,  // trits are multiples of 500 uS in size
        .long_width  = 500,  // trits are multiples of 500 uS in size
        .reset_limit = 2000, // this is short enough so we only get 1 row
        .decode_fn   = &rolling_code_decode,
        .disabled    = 1, // disabled and hidden, use 0 if there is a MIC, 1 otherwise
        .fields      = output_fields,
};
