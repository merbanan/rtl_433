/** @file
    Neptune r900 protocol decoder for r900 based flow meters, tested with my water meter.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
The device uses PPM encoding,
- 1 is encoded as 30 us pulse.
- 0 is encoded as 30 us gap.
A gap longer than 320 us is considered the end of the transmission.
The device sends a transmission every xx seconds.
A transmission starts with a preamble of 0xAA,0xAA,0xAA,0xAB,0x52,0xCC,0xD2
But, it is "zero" based, so if you add a zero bit to the payload, the preamble
is: 0x55,0x55,0x55,0x55,0xA9,0x66,0x69,0x65
It should be sufficient to find the start of the data after 0x55,0x55,0x55,0xA9,0x66,0x69,0x65.

Once the payload is decoded, the message is as follows:
              (from https://github.com/bemasher/rtlamr/wiki/Protocol#r900-consumption-message)
ID - 32 bits
Unkn1 - 8 bits
NoUse - 6 bits
BackFlow - 6 bits    // found this to be 2 bits in my case ???
Consumption - 24 bits
Unkn3 - 2 bits
Leak - 4 bits
LeakNow - 2 bits

after decoding the stream as described below, 104 bits of payload appears to be:
Data layout:
    IIIIIIII IIIIIIII IIIIIIII IIIIIIII UUUUUUUU NNNNNNBB CCCCCCCC CCCCCCCC CCCCCCCC UUTTTTLL EEEEEEEE EEEEEEEE EEEEEEEE

- I: 32-bit little-endian id
- U:  8-bit Unknown1
- N:  6-bit NoUse
- B:  2-bit backflow flag
- C: 24-bit Consumption Data
- U:  2-bit Unknown3
- T:  4-bit leak flag type
- L:  2-bit leak flag
- E: 24-bit extra data????
*/

#include "decoder.h"
#include <stdlib.h>

//
// for converting decimal into 5-bit binary representations
//
const char *bit_rep[32] = {
    [ 0] = "00000", [ 1] = "00001", [ 2] = "00010", [ 3] = "00011",
    [ 4] = "00100", [ 5] = "00101", [ 6] = "00110", [ 7] = "00111",
    [ 8] = "01000", [ 9] = "01001", [10] = "01010", [11] = "01011",
    [12] = "01100", [13] = "01101", [14] = "01110", [15] = "01111",
    [16] = "10000", [17] = "10001", [18] = "10010", [19] = "10011",
    [20] = "10100", [21] = "10101", [22] = "10110", [23] = "10111",
    [24] = "11000", [25] = "11001", [26] = "11010", [27] = "11011",
    [28] = "11100", [29] = "11101", [30] = "11110", [31] = "11111",
};

//
// convert a 4-bit char code from the bitbuffer into one base-6 bit char
//
static char get_bit_from_chip( char *chip ) {
    if ( strcmp(chip, "0011") == 0 )
        return '0';
    if ( strcmp(chip, "0101") == 0 )
        return '1';
    if ( strcmp(chip, "0110") == 0 )
        return '2';
    if ( strcmp(chip, "1100") == 0 )
        return '3';
    if ( strcmp(chip, "1010") == 0 )
        return '4';
    if ( strcmp(chip, "1001") == 0 )
        return '5';
    // error
    return 'x';    
}

//
// convert one 8-bit char stream into a base-6 decimal representation
//
static int get_base6_dec( char *chip_byte ) {
    char chip1[5] = {0};
    char chip2[5] = {0};
    char bit_1 = 'x';
    char bit_2 = 'x';
    int sum = 0;

    for ( int i = 0; i < 8; i++ ) {
        if ( i < 4 )
            chip1[i] = chip_byte[i];
        else
            chip2[i-4] = chip_byte[i];
    }
    bit_1 = get_bit_from_chip( chip1 );
    bit_2 = get_bit_from_chip( chip2 );
    if ( bit_1 == 'x' || bit_2 == 'x' )
        return -99;
    sum = 6 * ((int)bit_1 - 48) + ((int)bit_2 - 48);
    return sum;
}

//
// parse values from the reconstructed bitstream
//
static uint32_t parse_value( char *bb, int start, int end ) {
    char value[33] = {0}; // no values larger than 4 bytes

    for ( int i=start; i < end; i++ ) {
        value[i-start] = bb[i];
    }
    value[end-start] = 0;
    return strtol(value, NULL, 2);
}

static int r900_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{   // partial preamble and sync word shifted by 1 bit
    uint8_t const preamble[] = {0x55, 0x55, 0x55, 0xa9, 0x66, 0x69, 0x65};
    uint16_t preamble_bits = sizeof(preamble) * 8;

    uint32_t meter_id;
    uint32_t Unkn1;
    uint32_t NoUse;
    uint32_t BackFlow;
    uint32_t Consumption;
    uint32_t Unkn3;
    uint32_t Leak;
    uint32_t LeakNow;
    int row = 0;

    // Search for preamble and sync-word
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, preamble_bits);
    // No preamble detected
    if (start_pos == bitbuffer->bits_per_row[row])
        return DECODE_ABORT_EARLY;
    decoder_logf(decoder, 1, __func__, "r900 protocol detected, buffer is %d bits length", bitbuffer->bits_per_row[row]);
    
    // Remove preamble and sync word, keep whole payload
    uint8_t b[21];
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + preamble_bits, b, 21 * 8);

    // check that (bitbuffer->bits_per_row[0]) greater than (start_pos+sizeof(preamble)*8+168)
    if (start_pos+preamble_bits+168 > bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;

    uint8_t *bb = bitbuffer->bb[0];
    int base6_dec[21];
    char final_bits[106] = {0}; // 21 * 5 = 105 bits of data + 0
    int count = 0;

    /*
     * Each group of four of these chips must be interpretted as a digit in base 6 
	 *             according to the following mapping:
     * 0011 -> 0
     * 0101 -> 1
     * 0110 -> 2
     * 1100 -> 3
     * 1010 -> 4
     * 1001 -> 5
    */
    // create a pair of char bit array of '0' and '1' for each base6 byte
    for (uint16_t k = start_pos+preamble_bits; k < start_pos + preamble_bits + 168; k=k+8) {
        char chip_byte[9] = {0};
        for ( int j = 0; j < 8; j++) {
            int bit = bitrow_get_bit(bb, k+j);
            if (bit == 1)
                chip_byte[j] = '1';
            else
                chip_byte[j] = '0';
        }

        // convert the above to a base6 integer
        base6_dec[count] = get_base6_dec( chip_byte );
        if ( base6_dec[count] == -99 )
            return DECODE_ABORT_EARLY;

        count++;
    }

    // convert the base6 integers above into binary bits for decoding data
    for ( int i=0; i < 21; i++ ) {
        strncat(final_bits, bit_rep[base6_dec[i]], 5);
    }

    // decode the data
    //
    // meter_id 32 bits
    meter_id = parse_value( final_bits, 0, 32 );

    //Unkn1 8 bits
    Unkn1 = parse_value( final_bits, 32, 40 );

    //NoUse 6 bits
    NoUse = parse_value( final_bits, 40, 46 );

    //BackFlow 2 bits
    BackFlow = parse_value( final_bits, 46, 48 );

    //Consumption 24 bits
    Consumption = parse_value( final_bits, 48, 72 );

    //Unkn3 2 bits
    Unkn3 = parse_value( final_bits, 72, 74 );

    //Leak 4 bits
    Leak = parse_value( final_bits, 74, 78 );

    //LeakNow 2 bits
    LeakNow = parse_value( final_bits, 78, 80 );

    //extra 
    char value8[9];
    for ( int i=80; i < 88; i++ ) {
        value8[i-80] = final_bits[i];
    }
    value8[8] = 0;
    int eb1 = strtol(value8, NULL, 2);
    
    char value9[9];
    for ( int i=88; i < 96; i++ ) {
        value9[i-88] = final_bits[i];
    }
    value9[8] = 0;
    int eb2 = strtol(value9, NULL, 2);
    
    char value10[9];
    for ( int i=96; i < 104; i++ ) {
        value10[i-96] = final_bits[i];
    }
    value10[8] = 0;
    int eb3 = strtol(value10, NULL, 2);

    char extra[21];
    sprintf(extra,"%x %x %x", eb1, eb2, eb3);

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",    DATA_STRING, "R900",
            "id",          "",    DATA_INT,    meter_id,
            "unkn1",       "",    DATA_INT,    Unkn1,
            "nouse",       "",    DATA_INT,    NoUse,
            "backflow",    "",    DATA_INT,    BackFlow,
            "consumption", "",    DATA_INT,    Consumption,
            "unkn3",       "",    DATA_INT,    Unkn3,
            "leak",        "",    DATA_INT,    Leak,
            "leaknow",     "",    DATA_INT,    LeakNow,
            "mic",         "",    DATA_STRING, "CHECKSUM", // CRC, CHECKSUM, or PARITY
            "extra",       "",    DATA_STRING, extra,
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
        "id",
        "unkn1",
        "nouse",
        "backflow",
        "consumption",
        "unkn3",
        "leak",
        "leaknow",
        "mic", // remove if not applicable
        "extra",
        NULL,
};


/*
 * r_device - registers device/callback. see rtl_433_devices.h
 */
r_device r900 = {
        .name        = "Neptune r900 protocol",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 30,
        .long_width  = 30,
        .reset_limit = 320, // a bit longer than packet gap
        .decode_fn   = &r900_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
