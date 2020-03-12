/** @file
    Decoder for Rolling Code Transmitter

    Copyright (C) 2020 Flemi Oisterhocker

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
(this is a markdown formatted section to describe the decoder)
(describe the modulation, timing, and transmission, e.g.)
 * The device uses OOK_PULSE_PCM_RZ encoding,
 * The packet starts with either a narrow (500 uS) start pulse or a long (1500 uS) pulse.
 * 0 is defined as a 1500 uS gap to the next pulse.
 * 1 is defined as a 1000 uS gap to the next pulse.
 * 2 is defined as a 500 uS gap to the next pulse.

*/

#include <stdlib.h>
#include "decoder.h"

/*
 * Hypothetical template device
 *
 * Message is 68 bits long
 * Messages start with 0xAA
 * The message is repeated as 5 packets,
 * require at least 3 repeated packets.
 *
 */

#define MIN_BITS		80

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
    while (nibble_index < 20 && *bit_index <= num_bits - 4) {
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

    if (nibble_index != 20) {
        fprintf(stderr, "Not enough bits for 20 nibbles\n");

        return 1;
    } else {
        return 0;
    }
}

static void cleanup(uint8_t **ptr) {
    if (*ptr != NULL) {
       free(*ptr);
    }

    *ptr = NULL;
}

static int rolling_code_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // This holds a previous start width 1 packet for counter decoding
    // once we have a start width 3 packet.
    static uint8_t *prev_packet = NULL;

    data_t *data = NULL;
    int index = 0; // a row index
    uint8_t *b = NULL; // bits of a row
    int num_bits = 0;
    int start_width = 0;
    uint8_t trinary[20] = {0};
    int debug_output = decoder->verbose;

    if (debug_output > 1) {
    	bitbuffer_printf(bitbuffer, "%s: ", __func__);
    }

    if (bitbuffer->num_rows < 1) {
	cleanup(&prev_packet);

        return 0;
    }

    b = bitbuffer->bb[0];
    num_bits = bitbuffer->bits_per_row[0];

    start_width = get_start_bit_width(b, &index, num_bits, debug_output);

    if (start_width != 1 && start_width != 3) {
        if (debug_output > 1) {
            fprintf(stderr, "Start bit width invalid: %d\n", start_width);
        }

	cleanup(&prev_packet);

        return 0;
    }

    /*
     * Now that message "envelope" has been validated,
     * start parsing data.
     */

     if (get_trits(trinary, b, &index, num_bits, debug_output)) {
        fprintf(stderr, "get_trits failed\n");
	cleanup(&prev_packet);

        return 0;
    }
    
    printf("START WIDTH %d CODE: ", start_width);
    for (int i = 0; i < 20; i++) {
        printf("%d ", trinary[i]);
    }   
    printf("\n");
    
    char a_buffer[11] = {0};
    char r_buffer[11] = {0};

    printf("ACODE: ");
    for (int i = 0; i < 10; i++) {
	a_buffer[i] = '0' + trinary[i * 2];
        printf("%d ", trinary[i * 2]);
    }       
    printf("\n");

    printf("ROLL:  ");
    for (int i = 0; i < 10; i++) {
	r_buffer[i] = '0' + trinary[i * 2 + 1];
        printf("%d ", trinary[i * 2 + 1]);
    }
    printf("\n");

    if (start_width == 1) {
	prev_packet = malloc(sizeof(trinary));
        memcpy(prev_packet, trinary, sizeof(trinary));
    }

    int counter = -1;

    if (prev_packet != NULL && start_width == 3) {
        counter = 123;
    }
    
    /* clang-format off */
    data = data_make(
            "model", "", DATA_STRING, "Rolling Code Transmitter",
            "a_code","", DATA_STRING, a_buffer,
            "rolling_code",  "", DATA_STRING, r_buffer,
            "start_width", "", DATA_INT, start_width,
            "counter",   "", DATA_INT, counter,
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

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
        "a_code",
        "rolling_code",
	"start_width",
	"counter",
        NULL,
};

/*
 * r_device - registers device/callback. see rtl_433_devices.h
 *
 * Timings:
 *
 * short, long, and reset - specify pulse/period timings in [us].
 *     These timings will determine if the received pulses
 *     match, so your callback will fire after demodulation.
 *
 * Modulation:
 *
 * The function used to turn the received signal into bits.
 * See:
 * - pulse_demod.h for descriptions
 * - r_device.h for the list of defined names
 *
 * This device is disabled and hidden, it can not be enabled.
 *
 * To enable your device, add it to the list in include/rtl_433_devices.h
 * and to src/CMakeLists.txt and src/Makefile.am or run ./maintainer_update.py
 *
 */
r_device rolling_code = {
        .name        = "Rolling Code Transmitter",
        .modulation  = OOK_PULSE_PCM_RZ,
        .short_width = 500,  // short gap is 132 us
        .long_width  = 500,  // long gap is 224 us
        // .gap_limit   = 2000,  // some distance above long
        .reset_limit = 2000, // was 61500, // a bit longer than packet gap
        .decode_fn   = &rolling_code_decode,
        .disabled    = 1, // disabled and hidden, use 0 if there is a MIC, 1 otherwise
        .fields      = output_fields,
};
