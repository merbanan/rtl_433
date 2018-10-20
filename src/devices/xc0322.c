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


int fprintf_bits2csv(FILE * stream, uint8_t byte) {
    // Print binary values , 8 bits at a time
    int nprint = 0;
    
		for (uint16_t bit = 0; bit < 8; ++bit) {
		  if ((bit % 8) == 0)      // Separator to start a byte
		 	  nprint += fprintf(stream, "\t");
			if (byte & (0x80 >> (bit % 8))) {
		 	 nprint += fprintf(stream, "1");
		  } else {
		 	 nprint += fprintf(stream, "0");
		  }
		  if ((bit % 8) == 3)      // Separator between nibbles
		 	 nprint += fprintf(stream, " ");
		  if ((bit % 8) == 7)      // Separator to end a byte
		 	 nprint += fprintf(stream, ",");
		} 
		return nprint;
}

int fprintf_byte2csv(FILE * stream, char * label, uint8_t byte) {
    //Print hex and binary
    //fprintf_ing a tab character (\t) helps stop Excel stripping leading zeros
    int nprint = 0;
    nprint = fprintf(stream, "\t%s  %02X  ", label, byte);
    nprint += fprintf_bits2csv(stream, byte);
    return nprint;
}

char xc0322_label[7] = {0};

void get_xc0322_label(char * label) {
	// Get a 7 char label for this "line" of output read from stdin
	// Has a hissy fit if nothing there to read!!
	// EXTRA Bodgy!!
  if (fgets(label, 7, stdin) != NULL) {
    fprintf(stderr, "%s\n", label);
  }
}

 
void bitbuffer_print_csv(const bitbuffer_t *bits) {
	int highest_indent, indent_this_col, indent_this_row, row_len;
	uint16_t col, row;

	/* Figure out the longest row of bit to get the highest_indent
	 */
	//highest_indent = sizeof("[dd] {dd} ") - 1;
	//for (row = indent_this_row = 0; row < bits->num_rows; ++row) {
	//	for (col = indent_this_col  = 0; col < (bits->bits_per_row[row]+7)/8; ++col) {
	//		indent_this_col += 2+1;
	//	}
	//	indent_this_row = indent_this_col;
	//	if (indent_this_row > highest_indent)
	//		highest_indent = indent_this_row;
	//}
	
	// Label this "line" of output with 7 character label read from stdin
	// Has a hissy fit if nothing there to read!!
  //fprintf(stderr, "BEFORE%s<< %d ,", xc0322_label, (int)strlen(xc0322_label) );
  if (strlen(xc0322_label) == 0 ) get_xc0322_label(xc0322_label);
  fprintf(stderr, "%s ,", xc0322_label);

  // Filter out bad samples (too much noise, not enough sample)
  if ((bits->num_rows > 1) | (bits->bits_per_row[0] < 140)) {
		fprintf(stderr, "nr[%d] r[%02d] nsyn[%02d] nc[%02d] ,", 
                    bits->num_rows, 0, bits->syncs_before_row[0], bits->bits_per_row[0]);
    fprintf(stderr, "CORRUPTED data signal");
    return;
  }
  
	for (row = 0; row < bits->num_rows; ++row) {
		fprintf(stderr, "nr[%d] r[%02d] nsyn[%02d] nc[%2d] ,", 
                    bits->num_rows, row, bits->syncs_before_row[row], bits->bits_per_row[row]);
		for (col = row_len = 0; col < (bits->bits_per_row[row]+7)/8; ++col) {
		  if ((col % 68) == 67) fprintf(stderr, " | \n"); // Chunk into useful bytes per line
			fprintf_byte2csv(stderr, "", bits->bb[row][col]);
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

static const uint8_t preamble_pattern[1] = {0x5F}; // Only 8 bits

static uint8_t
calculate_checksum(uint8_t *buff, int length)
{
    uint8_t mask = 0x7C;
    uint8_t checksum = 0x64;
    uint8_t data;
    int byteCnt;

    for (byteCnt=0; byteCnt < length; byteCnt++) {
        int bitCnt;
        data = buff[byteCnt];

        for (bitCnt = 7; bitCnt >= 0 ; bitCnt--) {
            uint8_t bit;

            // Rotate mask right
            bit = mask & 1;
            mask = (mask >> 1 ) | (mask << 7);
            if (bit) {
                mask ^= 0x18;
            }

            // XOR mask into checksum if data bit is 1
            if (data & 0x80) {
                checksum ^= mask;
            }
            data <<= 1;
        }
    }
    return checksum;
}


static int
x0322_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[19];
    uint8_t brev[19];
    int deviceID;
    int isBatteryLow;
    int channel;
    float temperature;
    int humidity;
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;

    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, 19*8);
    
    for (int i = 0; i < 19; i++) {
      brev[i] = reverse8(b[i]);
    }

    // Lets look at the "aligned" data
    if (strlen(xc0322_label) == 0 ) get_xc0322_label(xc0322_label);
    fprintf(stderr, "\n%s||, , ", xc0322_label);
    
    
    for (int col = 0; col < 19; col++) {
    // Concentrate on the first 6 (of 18.5 possible) bytes
    //for (int col = 0; col < 6; col++) {
      fprintf_byte2csv(stderr, "", b[col]);
    	//fprintf(stderr, "\t%02X ,", b[col]);
      // Print binary values , 8 bits at a time
	 	  //fprintf_bits2csv(stderr, b[col]);
			if ((col % 4) == 3) fprintf(stderr, " | ");
    }

    // Decode temperature (b[2]), plus 1st 4 bits b[3], LSB order!
    // Tenths of degrees C, offset from the minimum possible (-40.0 degrees)
    
    uint16_t temp = ( (uint16_t)(reverse8(b[3]) & 0x0f) << 8) | reverse8(b[2]) ;
    temperature = (temp / 10.0f) - 40.0f ;
    fprintf(stderr, "Temp was %4.1f ,", temperature);

    //Let's look at b[5]

    // Finally, after many experiments  - Bingo
    // b[5] is a check byte. 
    // Each bit is the parity of the bits in corresponding position of b[0] to b[4]
    // Or brev[5] == brev[0] ^ brev[1] ^ brev[2] ^ brev[3] ^ brev[4]
    fprintf_byte2csv(stderr, "brev0 ^ brev1 ^ brev2 ^ brev3 ^ brev4", brev[0] ^ brev[1] ^ brev[2] ^ brev[3] ^ brev[4]);
    fprintf_byte2csv(stderr, "brev5", brev[5]);
    fprintf_byte2csv(stderr, "brev0 ^ brev1 ^ brev2 ^ brev3 ^ brev4 ^ brev5", brev[0] ^ brev[1] ^ brev[2] ^ brev[3] ^ brev[4] ^ brev[5]);

    fprintf(stderr, "\n");
    
    // The ambient weather checksum
    uint8_t expected = b[5];
    uint8_t calculated = calculate_checksum(brev, 5);

    //if (expected != calculated) {
       // if (debug_output) {
       //     fprintf(stderr, "Checksum error in xc0322 message.    Expected: %02x,    Calculated: %02x, ", expected, calculated);
    //        fprintf(stderr, "Message: ");
    //        for (int i=0; i < 6; i++)
    //            fprintf(stderr, "%02x ", b[i]);
    //        fprintf(stderr, "\n\n");
        //}
    //    return 0;
    //}
    fprintf(stderr, "ambient checksum Expected: %02x, Calculated: %02x, ", expected, calculated);
    

    /*
     * Check message integrity (CRC example)
     *
     * Example device uses CRC-8
     
    uint8_t c_crc0_5 = crc8(brev,     5, MYDEVICE_CRC_POLY, MYDEVICE_CRC_INIT);
    uint8_t c_crc1_5 = crc8(&brev[1], 4, MYDEVICE_CRC_POLY, MYDEVICE_CRC_INIT);
    fprintf(stderr, "crc8 5 bytes: %02x, 4 bytes no preamble: %02x, ", c_crc0_5, c_crc1_5);

    uint8_t c_crcle0_5 = crc8le(brev,     5, MYDEVICE_CRC_POLY, MYDEVICE_CRC_INIT);
    uint8_t c_crcle1_5 = crc8le(&brev[1], 4, MYDEVICE_CRC_POLY, MYDEVICE_CRC_INIT);
    fprintf(stderr, "crc8le 5 bytes: %02x, 4 bytes no preamble: %02x, ", c_crcle0_5, c_crcle1_5);

    fprintf(stderr, "\n");
    */
    /* 

    deviceID = b[1];
    isBatteryLow = (b[2] & 0x80) != 0; // if not zero, battery is low
    channel = ((b[2] & 0x70) >> 4) + 1;
    int temp_f = ((b[2] & 0x0f) << 8) | b[3];
    temperature = (temp_f - 400) / 10.0f;
    humidity = b[4];

    local_time_str(0, time_str);
    data = data_make(
            "time",           "",             DATA_STRING, time_str,
            "model",          "",             DATA_STRING, "Ambient Weather F007TH Thermo-Hygrometer",
            "device",         "House Code",   DATA_INT,    deviceID,
            "channel",        "Channel",      DATA_INT,    channel,
            "battery",        "Battery",      DATA_STRING, isBatteryLow ? "Low" : "Ok",
            "temperature_F",  "Temperature",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature,
            "humidity",       "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",            "Integrity",    DATA_STRING, "CRC",
            NULL);
    data_acquired_handler(data);
    */

    return 1;
}


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

    unsigned bitpos;
    int events = 0;

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

    r = 0;
    fprintf(stderr, "Geoff is working on row %d", r);

    b = bitbuffer->bb[r];

    /*
     * Either reject rows that don't start with the correct start byte:
     * Example message should start with 0xAA
     */
    //if (b[0] != MYDEVICE_STARTBYTE) {
    //    return 0;
    //}

    /*
     * Or (preferred) search for the message preamble:
     * See bitbuffer_search()
     * or copy the style from another file, eg ambient_weather.c :-)
     */
        bitpos = 0;
        fprintf(stderr, "\nbitpos starts at %03d", bitpos);
        bitpos = bitbuffer_search(bitbuffer, r, bitpos,
                (const uint8_t *)&preamble_pattern, 8);
        fprintf(stderr, "\nbitpos is now at %03d", bitpos);
        // Find a preamble with enough bits after it that it could be a complete packet
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, r, bitpos,
                (const uint8_t *)&preamble_pattern, 8)) + 8+16*8 <=
                bitbuffer->bits_per_row[r]) {
            events += x0322_decode(bitbuffer, r, bitpos ); //+ 8);
            if (events) return events; // for now, break after first successful message
            bitpos += 8;
            fprintf(stderr, "\n | loop bitpos is %03d", bitpos);
        }
     return 1;

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
    .reset_limit    = 300*4*2,
//    .json_callback  = &xc0322_callback,
    .json_callback  = &xc0322_template_callback,
};

