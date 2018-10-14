/* Template decoder for DEVICE, tested with BRAND, BRAND.
 *
 * Copyright (C) 2016 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * (describe the modulation, timing, and transmission, e.g.)
 * The device uses PPM encoding,
 * 0 is encoded as 102*4 us pulse and 129*4 us gap,
 * 1 is encoded as 102*4 us pulse and 158*4 us gap.
 * The device sends a transmission every 60 seconds.
 * I own 2 devices.
 * A transmission starts with a preamble of 0x5F,
 * 
 * REST IS YET TO BE REVERSE ENGINEERED
 there a 5 repeated packets, each with a 1200 us gap.
 *
 * (describe the data and payload, e.g.)
 * Packet nibbles:  FF PP PP II II II TT TT CC
 * F = flags, (0x40 is battery_low)
 * P = Pressure, 16-bit little-endian
 * I = id, 24-bit little-endian
 * T = Unknown, likely Temperature, 16-bit little-endian
 * C = Checksum, CRC-8 truncated poly 0x07 init 0x00
 *
 */

/* Use this as a starting point for a new decoder. */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

#include "bitbuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void bitbuffer_print_csv(const bitbuffer_t *bits) {
	int highest_indent, indent_this_col, indent_this_row, row_len;
	uint16_t col, row;

	/* Figure out the longest row of bit to get the highest_indent
	 */
	highest_indent = sizeof("[dd] {dd} ") - 1;
	for (row = indent_this_row = 0; row < bits->num_rows; ++row) {
		for (col = indent_this_col  = 0; col < (bits->bits_per_row[row]+7)/8; ++col) {
			indent_this_col += 2+1;
		}
		indent_this_row = indent_this_col;
		if (indent_this_row > highest_indent)
			highest_indent = indent_this_row;
	}
	// Label this "line" of output with 7 character label read from stdin
	// Has a hissy fit if nothing there to read!!
  char label[7];
  if (fgets(label, 7, stdin) != NULL) fprintf(stderr, "%s, ", label);

	// fprintf(stderr, "nr[%d] ", bits->num_rows);
  
  // Filter out bad samples (too much noise, not enough sample)
  if ((bits->num_rows > 1) | (bits->bits_per_row[0] < 140)) {
		fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bits->num_rows, 0, bits->bits_per_row[0]);
    fprintf(stderr, "CORRUPTED data signal");
    return;
  }
  
  // Try some different format interpretations 
	float* fptr;
	double* dptr;
	
	for (row = 0; row < bits->num_rows; ++row) {
		fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bits->num_rows, row, bits->bits_per_row[row]);
		for (col = row_len = 0; col < (bits->bits_per_row[row]+7)/8; ++col) {
		  if ((col % 68) == 67) fprintf(stderr, " | \n"); // Chunk into useful bytes per line
		  /*
      fprintf(stderr, "(%02d)", col);
      */
			row_len += fprintf(stderr, "%02X ,", bits->bb[row][col]);
	  	//fprintf(stderr, " --> %04d ,", bits->bb[row][col]);
	  	/*
	  	fptr = &(bits->bb[row][col]);
	  	fprintf(stderr, " --> %04f ", *fptr);
	  	dptr = &(bits->bb[row][col]);
	  	fprintf(stderr, " --> %04f ", *dptr);
	  	//fprintf(stderr, " --> %04f ", 23.0);
	  	*/
      // Print binary values , 8 bits at a time
	  	
		 	for (uint16_t bit = 0; bit < 8; ++bit) {
			  if ((bit % 8) == 0)      // Add byte separators
			  	fprintf(stderr, "0b ");
		  	if (bits->bb[row][col] & (0x80 >> (bit % 8))) {
			  	fprintf(stderr, "1");
			  } else {
			  	fprintf(stderr, "0");
			  }
			  if ((bit % 8) == 7)      // Add byte separators
			  	fprintf(stderr, ",");
		  } 

			if ((col % 4) == 3) fprintf(stderr, " | ");
		  
	  	//fprintf(stderr, "\n");
		}
	}
}


/*
 * XC0322 device
 *
 * Message is 148 bits long
 * Messages start with 0x5F
 * The message is ... ??? repeated as 5 packets,
 * ???? require at least 3 repeated packets.
 *
 */
#define MYDEVICE_BITLEN      148
#define MYDEVICE_STARTBYTE   0x5F
#define MYDEVICE_MINREPEATS  3
#define MYDEVICE_MSG_TYPE    0x10
#define MYDEVICE_CRC_POLY    0x07
#define MYDEVICE_CRC_INIT    0x00

static int xc0322_template_callback(bitbuffer_t *bitbuffer)
{
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    int r; // a row index
    uint8_t *b; // bits of a row
    int parity;
    uint8_t r_crc, c_crc;
    uint16_t sensor_id;
    uint8_t msg_type;
    int16_t value;

    /*
     * Early debugging aid to see demodulated bits in buffer and
     * to determine if your limit settings are matched and firing
     * this callback.
     *
     * 1. Enable with -D -D (debug level of 2)
     * 2. Delete this block when your decoder is working
     */
        if (debug_output > 1) {
            fprintf(stderr,"xc0322_template callback:\n");
            bitbuffer_print_csv(bitbuffer);
        }

    /*
     * If you expect the bits flipped with respect to the demod
     * invert the whole bit buffer.
     */

    //bitbuffer_invert(bitbuffer);

    /*
     * The bit buffer will contain multiple rows.
     * Typically a complete message will be contained in a single
     * row if long and reset limits are set correctly.
     * May contain multiple message repeats.
     * Message might not appear in row 0, if protocol uses
     * start/preamble periods of different lengths.
     */

    /*
     * Either, if you expect just a single packet
     * loop over all rows and collect or output data:
     */

    for (r = 0; r < bitbuffer->num_rows; ++r) {
        b = bitbuffer->bb[r];

        /*
         * Validate message and reject invalid messages as
         * early as possible before attempting to parse data.
         *
         * Check "message envelope"
         * - valid message length (use a minimum length to account
         *   for stray bits appended or prepended by the demod)
         * - valid preamble/device type/fixed bits if any
         * - Data integrity checks (CRC/Checksum/Parity)
         */

        // Filter out bad samples (too much noise, not enough sample)
        if (bitbuffer->num_rows > 1)  {
	  	      fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bitbuffer->num_rows, 0, bitbuffer->bits_per_row[0]);
            fprintf(stderr, "CORRUPTED data signal - too many rows");
            return 0;
        }


        if (bitbuffer->bits_per_row[r] < MYDEVICE_BITLEN) {
  		      fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bitbuffer->num_rows, 0, bitbuffer->bits_per_row[0]);
            fprintf(stderr, "CORRUPTED data signal - not enough bits");
            continue; // to the next row?  
            // but I've already bailed out if there is more than 1 row
        }

        /*
         * ... see below and replace `return 0;` with `continue;`
         */
    }

    /*
     * Or, if you expect repeated packets
     * find a suitable row:
     */

    //r = bitbuffer_find_repeated_row(bitbuffer, MYDEVICE_MINREPEATS, MYDEVICE_BITLEN);
    //if (r < 0 || bitbuffer->bits_per_row[r] > MYDEVICE_BITLEN + 16) {
    //    return 0;
    //}

    b = bitbuffer->bb[r];

    /*
     * Either reject rows that don't start with the correct start byte:
     * Example message should start with 0xAA
     */
    if (b[0] != MYDEVICE_STARTBYTE) {
        return 0;
    }

    /*
     * Or (preferred) search for the message preamble:
     * See bitbuffer_search()
     */

    /*
     * Check message integrity (Parity example)
     */
    // parity check: odd parity on bits [0 .. 67]
    // i.e. 8 bytes and a nibble.
    parity = b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4] ^ b[5] ^ b[6] ^ b[7]; // parity as byte
    parity = (parity >> 4) ^ (parity & 0xF); // fold to nibble
    parity ^= b[8] >> 4; // add remaining nibble
    parity = (parity >> 2) ^ (parity & 0x3); // fold to 2 bits
    parity = (parity >> 1) ^ (parity & 0x1); // fold to 1 bit

    if (!parity) {
        if (debug_output) {
            fprintf(stderr, "new_template parity check failed\n");
        }
        return 0;
    }

    /*
     * Check message integrity (Checksum example)
     */
    if (((b[0] + b[1] + b[2] + b[3] - b[4]) & 0xFF) != 0) {
        if (debug_output) {
            fprintf(stderr, "new_template checksum error\n");
        }
        return 0;
    }

    /*
     * Check message integrity (CRC example)
     *
     * Example device uses CRC-8
     */
    r_crc = b[7];
    c_crc = crc8(b, MYDEVICE_BITLEN / 8, MYDEVICE_CRC_POLY, MYDEVICE_CRC_INIT);
    if (r_crc != c_crc) {
        // example debugging output
        if (debug_output) {
            fprintf(stderr, "new_template bad CRC: calculated %02x, received %02x\n",
                    c_crc, r_crc);
        }

        // reject row
        return 0;
    }

    /*
     * Now that message "envelope" has been validated,
     * start parsing data.
     */
    msg_type = b[1];
    sensor_id = b[2] << 8 | b[3];
    value = b[4] << 8 | b[5];

    if (msg_type != MYDEVICE_MSG_TYPE) {
        /*
         * received an unexpected message type
         * could be a bad message or a new message not
         * previously seen.  Optionally log debug output.
         */
        return 0;
    }

    local_time_str(0, time_str);

    data = data_make(
            "time",  "", DATA_STRING, time_str,
            "model", "", DATA_STRING, "New Template",
            "id",    "", DATA_INT,    sensor_id,
            "data",  "", DATA_INT,    value,
            "mic",   "", DATA_STRING, "CHECKSUM", // CRC, CHECKSUM, or PARITY
            NULL);

    data_acquired_handler(data);

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
    "time",
    "model",
    "id",
    "data",
    "mic", // remove if not applicable
    NULL
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
 * - rtl_433.h for the list of defined names
 *
 * This device is disabled by default. Enable it with -R 61 on the commandline
 */
/*
r_device template = {
    .name          = "Template decoder",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = (224 + 132) / 2, // short gap is 132 us, long gap is 224 us
    .long_limit    = 224 + 132,
    .reset_limit   = (224 + 132) * 2,
    .json_callback = &template_callback,
    .disabled      = 1,
    .demod_arg     = 0,
    .fields        = output_fields,
};
*/

// GEOFFs code begins here

//#include "rtl_433.h"
//#include "pulse_demod.h"


static int xc0322_callback(bitbuffer_t *bitbuffer) {
    //printf("\n\nBEGINNING XC0322\n\n");
    //bitbuffer_print(bitbuffer);
    bitbuffer_print_csv(bitbuffer);
    //printf("\n\nENDING XC0322\n\n");

    return 0;
}

r_device xc0322 = {
    .name           = "XC0322",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 190*4,
    .long_limit     = 300*4,
    .reset_limit    = 350*4,
//    .json_callback  = &xc0322_callback,
    .json_callback  = &xc0322_template_callback,
};

