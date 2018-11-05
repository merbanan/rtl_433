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
 *   transmitted in least significant bit first order
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
// about using the debug messages from this device handler.
#include "digitech_xc0324.debug2csv.c"

static const uint8_t preamble_pattern[1] = {MYDEVICE_STARTBYTE};


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
decode_xc0324_message(bitbuffer_t *bitbuffer, unsigned row, uint16_t bitpos, 
                      data_t ** data)
    /// @param *data : returns the decoded information as a data_t * 
{   uint8_t b[MYMESSAGE_BYTELEN];
    char id [4] = {0};
    double temperature;
    uint8_t const_byte4_0x80;
    uint8_t XORchecksum; // == 0x00 for a good message
    char time_str[LOCAL_TIME_BUFLEN];
    
    // Extract the message
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, MYMESSAGE_BITLEN);

    //if (debug_output > 1) message2csv(stderr, bitbuffer, row, bitpos);

    // Examine the XORchecksum and bail out if not OK
    XORchecksum = calculate_XORchecksum(b, 6);
    if (XORchecksum != 0x00) {
       if (debug_output > 1) {
           message2csv(stderr, bitbuffer, row, bitpos);
           byte2csv(stderr, "Bad checksum status - not 0x00 but ", XORchecksum, 8);
           endcsvline(stderr);
       }
       return 0;
    }
    
    // Extract the id as hex string
    snprintf(id, 3, "%02X", b[1]);
    
    // Decode temperature (b[2]), plus 1st 4 bits b[3], LSB first order!
    // Tenths of degrees C, offset from the minimum possible (-40.0 degrees)
    uint16_t temp = ( (uint16_t)(reverse8(b[3]) & 0x0f) << 8) | reverse8(b[2]) ;
    temperature = (temp / 10.0) - 40.0 ;
    
    //Unknown byte, constant as 0x80 in all my data
    // ??maybe battery status??
    const_byte4_0x80 = b[4];
    
    time_t current;
    local_time_str(time(&current), time_str);
    *data = data_make(
            "time",           "Time",         DATA_STRING, time_str,
            "model",          "Device Type",  DATA_STRING, "Digitech XC0324",
            "id",             "ID",           DATA_STRING, id,
            "temperature_C",  "Temperature C",DATA_FORMAT, "%.1f", DATA_DOUBLE, temperature,
            "const_0x80",     "Constant ?",   DATA_INT,    const_byte4_0x80,
            "checksum_status","Checksum status",DATA_STRING, XORchecksum ? "Corrupted" : "OK",
            "mic",            "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);

    //  Send optional "debug to csv" lines.
    if (debug_output > 1) {
        message2csv(stderr, bitbuffer, row, bitpos);
        fprintf(stderr, "Temp was %4.1f ,", temperature);
        endcsvline(stderr);
    }
    if ((debug_output > 2) & !reference_values_written) {
        reference_values_written = 1;
        startcsvline(stderr, "XC0324:DDD Reference Values");
        fprintf(stderr, "Temperature %4.1f C, sensor id %s,", temperature, id);
        endcsvline(stderr);
    }

    return 1;
}


// List of fields that may appear in the `-F csv` output
static char *output_fields[] = {
    "time",
    "model",
    "id",
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
    uint16_t bitpos;
    int result;
    int events = 0;
    data_t * data;
    
    // Send a "debug to csv" formatted version of the bitbuffer to stderr.
    if (debug_output > 0) bitbuffer2csv(stderr, bitbuffer);
    if (debug_output > 2) reference_values_written = 0;
    
    //A clean XC0324 transmission contains 3 repeats of a message in a single row.
    //But in case of transmission or demodulation glitches, 
    //loop over all rows and check for recognisable messages.
    for (r = 0; r < bitbuffer->num_rows; ++r) {
        if (bitbuffer->bits_per_row[r] < MYMESSAGE_BITLEN) {
            // bail out of this row early (after an optional debug message)
            if (debug_output > 1) {
              message2csv(stderr, bitbuffer, r, 0);
              fprintf(stderr, "Bad row - %0d is too few bits for a message",
                      bitbuffer -> bits_per_row[r]);
              endcsvline(stderr);
            }
            continue; // to the next row  
        }
        // We have enough bits so search for a message preamble followed by 
        // enough bits that it could be a complete message.
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, r, bitpos,
                (const uint8_t *)&preamble_pattern, 8)) 
                + MYMESSAGE_BITLEN <= bitbuffer->bits_per_row[r]) {
            events += result = decode_xc0324_message(bitbuffer, r, bitpos, &data);
            if (result) {
              data_append(data, "message_num",  "Message repeat count", DATA_INT, events, NULL);
              data_acquired_handler(data);
              // Uncomment this `return` to break after first successful message,
              // instead of processing up to 3 repeats of the same message.
              //return events; 
            }
            bitpos += MYMESSAGE_BITLEN;
        }
    }
    if ((debug_output > 2) & !reference_values_written) {
        startcsvline(stderr, "XC0324:DDD Reference Values, Bad transmission,");
        endcsvline(stderr);
    }
    return events;
}


r_device digitech_xc0324 = {
    .name           = "Digitech XC-0324 temperature sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 190*4,// = (130 + 250)/2  * 4
    .long_limit     = 300*4,
    .reset_limit    = 300*4*2,
    .json_callback  = &xc0324_callback,
    .disabled       = 1, // stop debug output from spamming unsuspecting users
    .fields         = output_fields,
};

