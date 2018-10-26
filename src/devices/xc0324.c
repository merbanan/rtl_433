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
#include <signal.h>

#define MYDEVICE_BITLEN      148
#define MYMESSAGE_BITLEN     48
#define MYMESSAGE_BYTELEN    MYMESSAGE_BITLEN / 8
#define MYDEVICE_STARTBYTE   0x5F
#define MYDEVICE_MINREPEATS  3


/* 
  * Begin with some utility routines for examining messages
*/

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


// Get a label for my debuggng output
// Preferably read from stdin (bodgy but easy to arrange with a pipe)
// but failing that, after waiting 2 seconds, use a local time string

static volatile bool fgets_timeout;

void fgets_timeout_handler(int sigNum) {
    fgets_timeout = 1; // Non zero == True;
}

// Assume LOCAL_TIME_BUFLEN is at least 7!
char xc0322_label[LOCAL_TIME_BUFLEN] = {0};

void get_xc0322_label(char * label) {
	// Get a 7 char label for this "line" of output read from stdin
	// Has a hissy fit if nothing there to read!!
	// so set an alarm and provide a default alternative in that case.
  char * lab;

  // Allow fgets 2 seconds to read label from stdin
  fgets_timeout = 0; // False;
  signal(SIGALRM, fgets_timeout_handler);
  alarm(2);

  lab = fgets(label, 7, stdin);

  if (fgets_timeout) {
    // Use a current time string as a default label
    time_t current;
    local_time_str(time(&current), label);
  }
}


void bitbuffer_print_csv(const bitbuffer_t *bits) {
	uint16_t col, row;

	for (row = 0; row < bits->num_rows; ++row) {
    // I use the XC0322 in the stderr stream to select this csv line using 
    // grep "XC0322" 
    fprintf(stderr,"XC0322:DD Package, ");
  	// Label this "line" of output with (7) character label read from stdin
    fprintf(stderr, "%s, ", xc0322_label);
    //Echo the data from this row
		fprintf(stderr, "nr[%d] r[%02d] nsyn[%02d] nc[%2d] , , ", 
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


/*
 * XC0322 device
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
 * The -a -t -D -D output correctly guesses the older transmitter modulation 
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
 *   in tenths of degree celsius
 *   offset from -40.0 degrees C (minimum temp spec of the device)
 * byte 4 is constant (in all my data) 0x80
 *   ~maybe~ a battery status ???
 * byte 5 is a check byte (the XOR of bytes 0-4 inclusive)
 *   each bit is effectively a parity bit for correspondingly positioned bit in
 *   the real message
 *
 */
static const uint8_t preamble_pattern[1] = {0x5F}; // Only 8 bits

static uint8_t
calculate_paritycheck(uint8_t *buff, int length)
{
    // b[5] is a check byte. 
    // Each bit is the parity of the bits in corresponding position of b[0] to b[4]
    // ie brev[5] == brev[0] ^ brev[1] ^ brev[2] ^ brev[3] ^ brev[4]
    // and brev[0] ^ brev[1] ^ brev[2] ^ brev[3] ^ brev[4] ^ brev[5] == 0x00

    uint8_t paritycheck = 0x00;
    int byteCnt;

    for (byteCnt=0; byteCnt < length; byteCnt++) {
        paritycheck ^= buff[byteCnt];
    }
    // A clean message returns 0x00
    return paritycheck;
    
}


static int
xc0322_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos, data_t ** data)
{   // Working buffers
    uint8_t b[MYMESSAGE_BYTELEN];
    //uint8_t brev[MYMESSAGE_BYTELEN];
    
    // Extracted data values
    int deviceID;
    double temperature;
    uint8_t const_byte4_0x80;
    uint8_t parity_check; //message integrity check == 0x00
    char time_str[LOCAL_TIME_BUFLEN];
    

    // Extract the message
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, MYMESSAGE_BITLEN);
    // and reverse each byte for easier processing of temperature
    //for (int i = 0; i < MYMESSAGE_BYTELEN; i++) {
    //  brev[i] = reverse8(b[i]);
    //}

    if (debug_output > 0) {
      // Send the aligned data to stderr, in "debug to csv" format.
      for (int col = 0; col < MYMESSAGE_BYTELEN; col++) {
        fprintf_byte2csv(stderr, "", b[col]);
  			if ((col % 4) == 3) fprintf(stderr, " | ");
      }
    }

    // Examine the paritycheck and bail out if not OK
    //parity_check = calculate_paritycheck(brev, 6);
    parity_check = calculate_paritycheck(b, 6);
    if (parity_check != 0x00) {
       if (debug_output > 0) {
         // Close off the "debug to csv" line before giving up.
         fprintf_byte2csv(stderr, "Bad parity check - not 0x00 but ", parity_check);
         fprintf(stderr, "\n");
       }
       return 0;
    }
    
    // Extract the deviceID as int and as hex(arbitrary value?)
    deviceID = b[1];
    char id [4] = {0};
    snprintf(id, 3, "%02X", b[1]);
    
    // Decode temperature (b[2]), plus 1st 4 bits b[3], LSB order!
    // Tenths of degrees C, offset from the minimum possible (-40.0 degrees)
    
    uint16_t temp = ( (uint16_t)(reverse8(b[3]) & 0x0f) << 8) | reverse8(b[2]) ;
    temperature = (temp / 10.0) - 40.0 ;
    if (debug_output > 0) {
      fprintf(stderr, "Temp was %4.1f ,", temperature);
    }

    //Unknown byte, constant as 0x80 in all my data
    // ??maybe battery status??
    const_byte4_0x80 = b[4];

    //  Finish the "debug to csv" line.
    if (debug_output > 0) fprintf(stderr, "\n");

    time_t current;
    local_time_str(time(&current), time_str);
    *data = data_make(
            "time",           "Time",         DATA_STRING, time_str,
            "model",          "Device Type",  DATA_STRING, "Digitech XC0322",
            "id",             "ID",           DATA_STRING, id,
            "deviceID",       "Device ID",    DATA_INT,    deviceID,
            "temperature_C",  "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "const_0x80",       "Constant ?",   DATA_INT,    const_byte4_0x80,
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


static int xc0322_callback(bitbuffer_t *bitbuffer)
{
    //char time_str[LOCAL_TIME_BUFLEN];
    //data_t *data;
    int r; // a row index
    uint8_t *b; // bits of a row

    unsigned bitpos;
    int events = 0;
    data_t * data;
    int result;
    
    if (debug_output > 0) {
       // Slightly (well ok more than slightly) bodgy way to get file name 
       // labels for the "debug to csv" format outputs.
       if (strlen(xc0322_label) == 0 ) get_xc0322_label(xc0322_label);
    }


    /*
     * Early debugging aid to see demodulated bits in buffer and
     * to determine if your limit settings are matched and firing
     * this callback.
     *
     * 1. Enable with -D -D (debug level of 2)
     * 2. Delete this block when your decoder is working
     */
        if (debug_output > 1) {
            // Send a "debug to csv" formatted version of the whole packege
            // bitbuffer to stderr.
            bitbuffer_print_csv(bitbuffer);
        }
    /*
     * A complete XC0322 package contains 3 repeats of a message in a single row.
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
          if (debug_output > 0) {
            fprintf(stderr, "\nXC0322:D  Message, %s, ", xc0322_label);
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
            if (debug_output > 0) {
              // Start a "debug to csv" formatted version of one message's
              //worth of the bitbuffer to stderr.
              fprintf(stderr, "\nXC0322:D  Message, %s, ", xc0322_label);
	            fprintf(stderr, "nr[%d] r[%02d] nc[%2d] ,", bitbuffer->num_rows, r, bitbuffer->bits_per_row[r]);
              fprintf(stderr, "at bit [%03d], ", bitpos);
              // xc0322_decode will send the rest of the "debug to csv" formatted
              // to stderr.
            }
            events += result = xc0322_decode(bitbuffer, r, bitpos, &data);
            if (result) {
              data_append(data, "message_num",  "Message repeat count", DATA_INT, events, NULL);
              data_acquired_handler(data);
              //return events; // for now, break after first successful message
            }
            bitpos += MYMESSAGE_BITLEN;
        }
    }
    return events;
}

static int testhandler_callback(bitbuffer_t *bitbuffer)
{
  //EXPERIMENT
  // This works
  fprintf(stderr, "\nBefore data1\n");
  data_t *data1;
  data1 = data_make("foo1", "FOO1", DATA_DOUBLE, 42.0,
                    NULL);
  data_acquired_handler(data1);
  
  // This also works
  fprintf(stderr, "\nBefore data2\n");
  data_t *data2;
  data2 = data_make("bar2", "BAR2", DATA_STRING, "I am Bar2",
                    "more2", "MORE2", DATA_DATA, data_make("foo2", "FOO2", DATA_DOUBLE, 42.0,
                                                         NULL),
                    NULL);
  data_acquired_handler(data2);
  
  //But this seems to produce an infinite loop / segmentation fault
  fprintf(stderr, "\nBefore data3\n");
  data_t *data3;
  data3 = data_make("bar3", "BAR3", DATA_STRING, "I am Bar3",
                    "more3", "MORE3", DATA_DATA, data1,
                    NULL);
  data_acquired_handler(data3);
  
  fprintf(stderr, "\nBefore return\n");
  return 0;
}


r_device xc0322 = {
    .name           = "XC0322",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 190*4,
    .long_limit     = 300*4,
    .reset_limit    = 300*4*2,
//    .json_callback  = &xc0322_callback,
    .json_callback  = &testhandler_callback,
    .disabled       = 1, // stop the debug output from spamming unsuspecting users
    .fields        = output_fields,
};

