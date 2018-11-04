/* Decoder for Digitech XC-0324 temperature sensor, 
 *  tested with two transmitters.
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
 * The oldest transmits a "clean" pulse (ie a captured pulse, examined using
 * Audacity, has pretty stable I and Q values, ie little phase wandering)
 * The newer transmitter seems less stable (ie within a single pulse, the I and
 * Q components of the pulse signal appear to "rotate" through several cycles).
 * The -A -D -D output correctly guesses the older transmitter modulation 
 * and gap parameters, but mistakes the newer transmitter as pulse width 
 * modulation with "far too many" very short pulses.
 * 
 * A package is 148 bits 
 * (plus or minus one or two due to demodulation or transmission errors)
 * 
 * Each package contains 3 repeats of the basic 48 bit message,
 * with 2 zero bits separating each repetition.
 * 
 * A 48 bit message comsists of :
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

//***************************************************************************//
// USING THE DEBUG MESSAGES from this device handler.
//
// My debugging / deciphering strategy (copied from several helpful blog posts)
// involves saving a set of package files (using the `-a -t` arguments)
// then running my 'under development' device against the set of them, using 
// `-D` or `-DD` or `-DDD`arguments.
//
// The debug messages in this device emit a "debug to csv" format line.
// This is mainly a hex and bit pattern version of (part of) the bitbuffer.
// `-D` echoes the whole package, row by row.
// `-DD` echoes one line per decoded message from a package. (ie 3 up to lines)
// `-DDD` echoes the decoded reference values (temperature and sensor id).
//
// Here's a sample extract of part of a "debug to csv" line:
//
// , 	  5F  	0101 1111,	  64  	0110 0100,	  CC  	1100 1100,	  40  	0100 0000,	  80  	1000 0000,	  37  	0011 0111,
//
// The start of the line also includes some labels which 
//    a) let me use grep to select only the csv lines I want and 
//    b) tell me which line came from which test file.
//
// I include "XC0324:D" in the stderr stream to flag these "debug to csv" lines
// for grep.
//
// I use a bash script (I call mine exam.sh) to process my test files in batches:
//
    //exam.sh
    ///```
    //#! /bin/bash
    //
    //for f in g*.cu8;
    //do
    //  printf "$f" | ../../src/rtl_433 -R 110 -r $f $1 $2 $3 $4 $5
    //done
    //```
//
// which I use as follows:

//
    //
    //./exam.sh -D 2>&1 | grep "XC0324:" > xc0324.csv
    //
//
// I use `-DD` or `-DDD` for progressively more detail, and adjust the grep 
// pattern to select only those lines I require at a particular point in the
// reverse engineering process.
//
// Next I open xc0324.csv in a spreadsheet package (eg Excel), 
// manually edit in the correct observed temperatures for each test file,
// sort into observed temperature order, 
// and start looking for patterns.
//

//***************************************************************************//
/* 
  * A utility routine for obtaining meaningful labels for lines in my 
  * "debug to csv" format
*/
//***************************************************************************//

// Get a (filename) label for my debuggng output.
// Preferably read from stdin (bodgy but easy to arrange with a pipe)
// but failing that, after waiting 2 seconds, use a local time string

static volatile bool fgets_timeout;

void fgets_timeout_handler(int sigNum) {
    fgets_timeout = 1; // Non zero == True;
}

// Make sure the buffer is well and truly big enough
char xc0324_label[LOCAL_TIME_BUFLEN + 48] = {0};

void get_xc0324_label() {
	// Get a label for this "line" of output read from stdin.
	// fgets has a hissy fit and blocks if nothing there to read!!
	// so set an alarm and provide a default alternative in that case.
  if (strlen(xc0324_label) > 0 ) {
    return; //The label has already been read!
  }
  // Allow fgets 2 seconds to read label from stdin
  fgets_timeout = 0; // False;
  signal(SIGALRM, fgets_timeout_handler);
  alarm(2);
  char * lab = fgets(&xc0324_label[0], 48, stdin);
  xc0324_label[strcspn(xc0324_label, "\n")] = 0; //remove trailing newline
  if (fgets_timeout) {
    // Use a current time string as a default label
    time_t current;
    local_time_str(time(&current), &xc0324_label[0]);
  }
}

//***************************************************************************//
//
// Some utility routines for printing the bitbuffer in "debug to csv" format.
//
//***************************************************************************//

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

// Print all the rows in the bitbuffer in "debug to csv" format.
void bitbuffer_print_csv(const bitbuffer_t *bits) {
	uint16_t col, row;

	for (row = 0; row < bits->num_rows; ++row) {
  	// Label this "line" of csv output with a filename label read from stdin.
    
    fprintf(stderr, "%s, XC0324:D Package, ", xc0324_label);
    //Echo the data from this row
		fprintf(stderr, "nr[%d] r[%02d] nsyn[%02d] nc[%2d] ,at bit [000], ", 
                    bits->num_rows, row, bits->syncs_before_row[row], bits->bits_per_row[row]);
		for (col = 0; col < (bits->bits_per_row[row]+7)/8; ++col) {
			fprintf_byte2csv(stderr, "", bits->bb[row][col]);
		}
    // Flag bad samples (too much noise, not enough sample, 
    // or package possibly segmented over multiple rows
    if (bits->num_rows > 1) {
      fprintf(stderr, "Bad package - more than 1 row, ");
      // But maybe there are usable fragments somewhere?
    }
    if (bits->bits_per_row[row] < MYDEVICE_BITLEN) {
      fprintf(stderr, "Bad package - less than %d bits, ", MYDEVICE_BITLEN);
      // Mmmm, not a full package, but is there a single message?
    }
    if (bits->bits_per_row[row] < MYMESSAGE_BITLEN) {
      fprintf(stderr, "Bad message - less than %d bits, ", MYMESSAGE_BITLEN);
      // No, not even a single message :-)
    }
    fprintf(stderr, "\n");
	}
}

// Declare the function which processes the `-DDD` csv format line
// Code it later so it can share functions with the .json_callback function.
static int xc0324_referenceValues2csv(bitbuffer_t *bitbuffer);

//***************************************************************************//
//
// End of debugging utilities
//
//***************************************************************************//

static const uint8_t preamble_pattern[1] = {0x5F}; // Only 8 bits

static uint8_t
calculate_paritycheck(uint8_t *buff, int length)
{
    // b[5] is a check byte. 
    // Each bit is the parity of the bits in corresponding position of b[0] to b[4]
    // ie b[5] == b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4]
    // and b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4] ^ b[5] == 0x00

    uint8_t paritycheck = 0x00;
    int byteCnt;

    for (byteCnt=0; byteCnt < length; byteCnt++) {
        paritycheck ^= buff[byteCnt];
    }
    // A clean message returns 0x00
    return paritycheck;
    
}


static int
xc0324_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos, data_t ** data)
{   // Working buffer
    uint8_t b[MYMESSAGE_BYTELEN];
    
    // Extracted data values
    int deviceID;
    char id [4] = {0};
    double temperature;
    uint8_t const_byte4_0x80;
    uint8_t parity_check; //parity check == 0x00 for a good message
    char time_str[LOCAL_TIME_BUFLEN];
    

    // Extract the message
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, MYMESSAGE_BITLEN);

    if (debug_output > 1) {
      // Send the aligned (message) data to stderr, in "debug to csv" format.
      for (int col = 0; col < MYMESSAGE_BYTELEN; col++) {
        fprintf_byte2csv(stderr, "", b[col]);
  			if ((col % 4) == 3) fprintf(stderr, " | ");
      }
    }

    // Examine the paritycheck and bail out if not OK
    parity_check = calculate_paritycheck(b, 6);
    if (parity_check != 0x00) {
       if (debug_output > 1) {
         // Close off the "debug to csv" line before giving up.
         fprintf_byte2csv(stderr, "Bad parity check - not 0x00 but ", parity_check);
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
            "temperature_C",  "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "const_0x80",     "Constant ?",   DATA_INT,    const_byte4_0x80,
            "parity_status",  "Parity",       DATA_STRING, parity_check ? "Corrupted" : "OK",
            "mic",            "Integrity",    DATA_STRING, "PARITY",
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
    "parity_status",
    "mic",
    "message_num",
    NULL
};


static int xc0324_callback(bitbuffer_t *bitbuffer)
{
    int r; // a row index
    uint8_t *b; // bits of a row
    unsigned bitpos;
    int result;
    int events = 0;
    data_t * data;
    
    if (debug_output > 0) {
       // Slightly (well ok, more than slightly) bodgy way to get file name 
       // labels for the "debug to csv" format outputs.
       if (strlen(xc0324_label) == 0 ) get_xc0324_label();
    }
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
            bitbuffer_print_csv(bitbuffer);
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
            fprintf(stderr, "\n%s, XC0324:DD  Message, ", xc0324_label);
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
              fprintf(stderr, "\n%s, XC0324:DD  Message, ", xc0324_label);
	            fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bitbuffer->num_rows, r, bitbuffer->bits_per_row[r]);
              fprintf(stderr, "at bit [%03d], ", bitpos);
              // xc0324_decode will send the rest of the "debug to csv" formatted
              // to stderr.
            }
            events += result = xc0324_decode(bitbuffer, r, bitpos, &data);
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

