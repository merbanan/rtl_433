/* Decoder for Digitech XC-0324 temperature sensor. 
 *
 * Copyright (C) 2018 Geoff Lee
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
 
/*
 * XC0324 device
 *
 * The encoding is pulse position modulation 
 *(ie gap width contains the modulation information)
 * pulse is about 100*4 us
 * short gap is (approx) 130*4 us
 * long gap is (approx) 250*4 us
 
 * Deciphered using two transmitters.
 * 
 * A transmission package is 148 bits 
 * (plus or minus one or two due to demodulation or transmission errors)
 * 
 * Each transmission contains 3 repeats of the 48 bit message,
 * with 2 zero bits separating each repetition.
 * 
 * A 48 bit message consists of :
 * byte 0 = preamble (for synchronisation), 0x5F
 * byte 1 = device id
 * byte 2 and the first nibble of byte 3 encode the temperature 
 *    as a 12 bit integer,
 *   transmitted in **least significant bit first** order
 *   in tenths of degree Celsius
 *   offset from -40.0 degrees C (minimum temp spec of the device)
 * byte 4 is constant (in all my data) 0x80
 *   ~maybe~ a battery status ???
 * byte 5 is a check byte (the XOR of bytes 0-4 inclusive)
 *   each bit is effectively a parity bit for correspondingly positioned bit in
 *   the real message
 *
 */


#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

#include "bitbuffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define MYDEVICE_BITLEN      148
#define MYMESSAGE_BITLEN     48
#define MYMESSAGE_BYTELEN    MYMESSAGE_BITLEN / 8
#define MYDEVICE_STARTBYTE   0x5F
#define MYDEVICE_MINREPEATS  3


// See the (forthcoming) tutorial entry in the rtl_433 wiki for information 
// about using the debug messagesfrom this device handler.

// **** debug to csv utility functions ****

static volatile bool fgets_timeout;

void fgets_timeout_handler(int sigNum) {
    fgets_timeout = 1; // Non zero == True;
}

char csv_label[LOCAL_TIME_BUFLEN + 48] = {0};

void get_csv_label() {
	// Get a label for this "line" of output read from stdin.
	// In case stdin is empty, use a timestamp instead.
  if (strlen(csv_label) > 0 ) {
    return; //The label has already been read!
  }
  // Allow fgets 2 seconds to read label from stdin
  fgets_timeout = 0; // False;
  signal(SIGALRM, fgets_timeout_handler);
  alarm(2);
  char * lab = fgets(&csv_label[0], 48, stdin);
  csv_label[strcspn(csv_label, "\n")] = 0; //remove trailing newline
  if (fgets_timeout) {
    // Use a current time string as a default label
    time_t current;
    local_time_str(time(&current), &csv_label[0]);
  }
}


int bits2csv(FILE * stream, const uint8_t byte, const uint16_t numbits) {
    // Print binary values , 8 bits at a time

    int nprint = 0;
    
		for (uint16_t bit = 0; bit < 8; ++bit) {
		  if ((bit % 8) == 0)      // Separator to start a byte
		 	  nprint += fprintf(stream, "\t");
		 	if (bit < numbits) {
  			if (byte & (0x80 >> (bit % 8))) {
  		 	  nprint += fprintf(stream, "1");
  		  } else {
  		 	  nprint += fprintf(stream, "0");
  		  }
      } else {
         nprint += fprintf(stream, "-");
      }
		  if ((bit % 8) == 3)      // Separator between nibbles
		 	 nprint += fprintf(stream, " ");
		  if ((bit % 8) == 7)      // Separator to end a byte
		 	 nprint += fprintf(stream, ",");
		} 
		return nprint;
}

int byte2csv(FILE * stream, char * label, const uint8_t byte, 
             const uint16_t numbits) {
    //Print hex and binary is a csv column
    uint8_t maskedbyte, maskshift;
    if (numbits >= 8) {
      maskedbyte = byte;
    } else {
      maskshift = (8 - numbits);
      maskedbyte = (byte >> maskshift) << maskshift;
    };
    //fprintf_ing a tab character (\t) helps stop Excel stripping leading zeros
    int nprint = 0;
    nprint = fprintf(stream, "\t %s  %02X  ", label, maskedbyte);
    nprint += bits2csv(stream, maskedbyte, numbits);
    return nprint;
}

void startcsvline(FILE * stream, const char * line_label) {
    // Slightly (well ok, more than slightly) bodgy way to get file name 
    // labels for the "debug to csv" format outputs.
    if (strlen(csv_label) == 0 ) get_csv_label();
    fprintf(stream, "\n%s, %s, ", csv_label, line_label);
}

void endcsvline(FILE * stream) {
    fprintf(stream, "\n");
}

void partrow2csv(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos, const uint16_t numbits){
    uint8_t b[BITBUF_COLS];
    uint16_t col, bits_available, bitsleft;
    // Extract the part row
    if (bitpos + numbits <= bits -> bits_per_row[row]) {
      bits_available = numbits;
    } else {
      bits_available = bits -> bits_per_row[row] - bitpos;
    }
    bitbuffer_extract_bytes(bits, row, bitpos, b, bits_available);
    // Display the part row
		fprintf(stream, "nr[%d] r[%02d] nsyn[%02d] nc[%2d] ,at bit [%03d], ", 
                    bits->num_rows, row, bits->syncs_before_row[row], bits->bits_per_row[row],
                    bitpos);
		for (col = 0; col < (bits_available+7)/8; ++col) {
		  bitsleft = bits_available - col * 8;
			byte2csv(stream, "", b[col], bitsleft);
		}
    // Flag bad samples (too much noise, not enough sample, 
    // or package possibly segmented over multiple rows
    if (bits->num_rows > 1) {
      fprintf(stream, "Bad XC0324 package - more than 1 row, ");
      // But maybe there are usable fragments somewhere?
    }
    if (bits->bits_per_row[row] < MYDEVICE_BITLEN) {
      fprintf(stream, "Bad XC0324 package - less than %d bits, ", MYDEVICE_BITLEN);
      // Mmmm, not a full package, but is there a single message?
    }
    if (bits->bits_per_row[row] < MYMESSAGE_BITLEN) {
      fprintf(stream, "Bad XC0324 message - less than %d bits, ", MYMESSAGE_BITLEN);
      // No, not even a single message :-(
    }
}



void row2csv(FILE * stream, bitbuffer_t *bits, const uint16_t row){
    partrow2csv(stream, bits, row, 0, bits -> bits_per_row[row]);
}

void message2csv(FILE * stream, bitbuffer_t *bits, const uint16_t row,
                 const uint16_t bitpos){
   startcsvline(stream, "XC0324:DD Message");
   partrow2csv(stream, bits, row, bitpos, MYDEVICE_BITLEN);
}

void bitbuffer2csv(FILE * stream, bitbuffer_t *bits) {
  // Print all the rows in the bitbuffer in "debug to csv" format.
	uint16_t row;
	for (row = 0; row < bits->num_rows; ++row) {
    startcsvline(stream, "XC0324:D Package");
    partrow2csv(stream, bits, row, 0, bits -> bits_per_row[row]);
    endcsvline(stream);
	}
}

// Declare the function which processes the `-DDD` csv format line
// Code it later so it can share functions with the .json_callback function.
static int xc0324_referenceValues2csv(bitbuffer_t *bitbuffer);

// End of debugging utilities

static const uint8_t preamble_pattern[1] = {0x5F};

static uint8_t 
calculate_XORchecksum(uint8_t *buff, int length)
{
    // b[5] is a check byte, the XOR of bytes 0-4.
    // ie a checksum where the sum is "binary add no carry"
    // Effectively, each bit of b[5] is the parity of the bits in the 
    // corresponding position of b[0] to b[4]
    // NB : b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4] ^ b[5] == 0x00 for a clean message
    uint8_t XORchecksum = 0x00;
    int byteCnt;
    for (byteCnt=0; byteCnt < length; byteCnt++) {
        XORchecksum ^= buff[byteCnt];
    }
    return XORchecksum;
    
}

static int
decode_xc0324_message(bitbuffer_t *bitbuffer, unsigned row, uint16_t bitpos, data_t ** data)
    /// @param *data : returns the decoded information as a data_t * 
{   uint8_t b[MYMESSAGE_BYTELEN];
    int deviceID;
    char id [4] = {0};
    double temperature;
    uint8_t const_byte4_0x80;
    uint8_t XORchecksum; // == 0x00 for a good message
    char time_str[LOCAL_TIME_BUFLEN];
    

    // Extract the message
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, MYMESSAGE_BITLEN);

    if (debug_output > 1) {
      // Send the aligned (message) data to stderr, in "debug to csv" format.
      for (int col = 0; col < MYMESSAGE_BYTELEN; col++) {
        byte2csv(stderr, "", b[col], 8);
  			if ((col % 4) == 3) fprintf(stderr, " | ");
      }
    }

    // Examine the XORchecksum and bail out if not OK
    XORchecksum = calculate_XORchecksum(b, 6);
    if (XORchecksum != 0x00) {
       if (debug_output > 1) {
         // Close off the "debug to csv" line before giving up.
         byte2csv(stderr, "Bad checksum status - not 0x00 but ", XORchecksum, 8);
         fprintf(stderr, "\n");
       }
       return 0;
    }
    
    // Extract the deviceID as int and as hex(arbitrary value?)
    deviceID = b[1];
    snprintf(id, 3, "%02X", b[1]);
    
    // Decode temperature (b[2]), plus 1st 4 bits b[3], LSB first order!
    // Tenths of degrees C, offset from the minimum possible (-40.0 degrees)
    
    uint16_t temp = ( (uint16_t)(reverse8(b[3]) & 0x0f) << 8) | reverse8(b[2]) ;
    temperature = (temp / 10.0) - 40.0 ;
    if (debug_output > 1) {
      fprintf(stderr, "Temp was %4.1f ,", temperature);
    }

    //Unknown byte, constant as 0x80 in all my data
    // ??maybe battery status??
    const_byte4_0x80 = b[4];

    //  Finish the "debug to csv" line.
    if (debug_output > 1) fprintf(stderr, "\n");

    time_t current;
    local_time_str(time(&current), time_str);
    *data = data_make(
            "time",           "Time",         DATA_STRING, time_str,
            "model",          "Device Type",  DATA_STRING, "Digitech XC0324",
            "id",             "ID",           DATA_STRING, id,
            "deviceID",       "Device ID",    DATA_INT,    deviceID,
            "temperature_C",  "Temperature C",DATA_FORMAT, "%.1f", DATA_DOUBLE, temperature,
            "const_0x80",     "Constant ?",   DATA_INT,    const_byte4_0x80,
            "checksum_status","Checksum status",DATA_STRING, XORchecksum ? "Corrupted" : "OK",
            "mic",            "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);

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
    "deviceID",
    "temperature_C",
    "const_0x80",
    "checksum_status",
    "mic",
    "message_num",
    NULL
};


static int xc0324_callback(bitbuffer_t *bitbuffer)
{
    int r; // a row index
    uint8_t *b; // bits of a row
    uint16_t bitpos;
    int result;
    int events = 0;
    data_t * data;
    
    /*
     * Debugging aid to see demodulated bits in buffer and
     * to determine if the limit settings are matched and firing
     * this callback.
     *
     * 1. Enabled with -D  (debug level of 1)
     */
        if (debug_output > 0) {
            // Send a "debug to csv" formatted version of the whole
            // bitbuffer to stderr.
            bitbuffer2csv(stderr, bitbuffer);
        }
    /*
     * A complete XC0324 package contains 3 repeats of a message in a single row.
     * But there may be transmission or demodulation glitches, and so perhaps
     * the bit buffer could contain multiple rows.
     * So, check multiple row bit buffers just in case the full package,
     * or (more likely) a single repeat of the message, can be found.
     *
     * Loop over all rows and check for recognisable messages:
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
        if (bitbuffer->bits_per_row[r] < MYMESSAGE_BITLEN) {
          if (debug_output > 1) {
            // -DD or above will attempt to send one line in csv format
            // to stderr for each message encountered.  This just reports 
            // that we haven't found a good message on this row :-(
            fprintf(stderr, "\n%s, XC0324:DD  Message, ", csv_label);
            fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bitbuffer->num_rows, r, bitbuffer->bits_per_row[r]);
            fprintf(stderr, "Bad row - too few bits for a message\n");
          }
          continue; // to the next row  
        }
        // OK, at least we have enough bits
        /*
         * Search for a message preamble followed by enough bits 
         * that it could be a complete message:
         */
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, r, bitpos,
                (const uint8_t *)&preamble_pattern, 8)) 
                + MYMESSAGE_BITLEN <= bitbuffer->bits_per_row[r]) {
            if (debug_output > 1) {
              // -DD or above will attempt to send one line in csv format
              // to stderr for each message encountered.
              // This starts the "debug to csv" formatted version of one 
              // message's worth of the bitbuffer to stderr.
              fprintf(stderr, "\n%s, XC0324:DD  Message, ", csv_label);
	            fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bitbuffer->num_rows, r, bitbuffer->bits_per_row[r]);
              fprintf(stderr, "at bit [%03d], ", bitpos);
              // decode_xc0324_message will send the rest of the "debug to csv" formatted
              // to stderr.
            }
            events += result = decode_xc0324_message(bitbuffer, r, bitpos, &data);
            if (result) {
              data_append(data, "message_num",  "Message repeat count", DATA_INT, events, NULL);
              data_acquired_handler(data);
              // I thought I ought to release memory after I'm finished with it.
              // BUT if I use the following line I get a runtime error
              // munmap_chunk():  invalid pointer
              // SO DO NOT UNCOMMENT THE data_free call until I know more !!!
              // data_free(data);
              
              // Uncomment this `return` to break after first successful message,
              // instead of processing up to 3 repeats of the same message.
              //return events; 
            }
            bitpos += MYMESSAGE_BITLEN;
        }
    }
    // -DDD or above (debug_output >=3) will send a line in csv format
    // to stderr containing the correct reference values, and  either:
    //    the 
    // for that file (if run with the -r <filename>, and with <filename>
    if (debug_output >2) {
      int val2csv = xc0324_referenceValues2csv(bitbuffer);
    }
    return events;
}

// Include the code for the -DDD format "debug to csv" line
#include "xc0324.correctvalues.c"


r_device digitech_xc0324 = {
    .name           = "Digitech XC-0324 temperature sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 190*4,// = (130 + 250)/2  * 4
    .long_limit     = 300*4,
    .reset_limit    = 300*4*2,
    .json_callback  = &xc0324_callback,
    .disabled       = 1, // stop my debug output from spamming unsuspecting users
    .fields        = output_fields,
};

