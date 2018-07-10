/* Wireless M-Bus (EN 13757-4)
 *
 * Copyright (C) 2018 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "util.h"


// Mapping from 6 bits to 4 bits. "3of6" coding used for Mode T
static uint8_t m_bus_decode_3of6(uint8_t byte) {
    uint8_t out = 0xFF; // Error
fprintf(stderr,"Decode %0X\n", byte);
    switch(byte) {
        case 22:    out = 0x0;  break;  // 0x16
        case 13:    out = 0x1;  break;  // 0x0D
        case 14:    out = 0x2;  break;  // 0x0E
        case 11:    out = 0x3;  break;  // 0x0B
        case 28:    out = 0x4;  break;  // 0x17
        case 25:    out = 0x5;  break;  // 0x19
        case 26:    out = 0x6;  break;  // 0x1A
        case 19:    out = 0x7;  break;  // 0x13
        case 44:    out = 0x8;  break;  // 0x2C
        case 37:    out = 0x9;  break;  // 0x25
        case 38:    out = 0xA;  break;  // 0x26
        case 35:    out = 0xB;  break;  // 0x23
        case 52:    out = 0xC;  break;  // 0x34
        case 49:    out = 0xD;  break;  // 0x31
        case 50:    out = 0xE;  break;  // 0x32
        case 41:    out = 0xF;  break;  // 0x29
        default:    break;  // Error
    }
    return out;
}

// Decode input 6 bit nibbles to output 4 bit nibbles (packed in bytes). "3of6" coding used for Mode T
static int m_bus_decode_3of6_buffer(const bitrow_t bits, unsigned bit_offset, uint8_t* output, unsigned num_bytes) {
    for (unsigned n=0; n<num_bytes; ++n) {
        fprintf(stderr,"Decode %u, %u\n", n, bit_offset);
        uint8_t nibble_h = m_bus_decode_3of6(bitrow_get_byte(bits, n*12+bit_offset) >> 2);
        uint8_t nibble_l = m_bus_decode_3of6(bitrow_get_byte(bits, n*12+bit_offset+6) >> 2);
        if (nibble_h > 0xF || nibble_l > 0xF) {
            return -1;  // Decode error!
        }
        output[n] = (nibble_h << 4) | nibble_l;
    }
    return 0;
}


// Decode two bytes into three letters of five bits
static void m_bus_manuf_decode(uint16_t m_field, char* three_letter_code) {
    three_letter_code[0] = (m_field >> 10 & 0x1F) + 0x40;
    three_letter_code[1] = (m_field >> 5 & 0x1F) + 0x40;
    three_letter_code[2] = (m_field & 0x1F) + 0x40;
    three_letter_code[3] = 0;
}


static int m_bus_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    static const uint8_t HEADER_T[]  = { 0x55, 0x54, 0x3D};              // Mode T Header
//  static const uint8_t HEADER_CA[] = { 0x55, 0x54, 0x3D, 0x54, 0xCD};  // Mode C, format A Header
//  static const uint8_t HEADER_CB[] = { 0x55, 0x54, 0x3D, 0x54, 0x3D};  // Mode C, format B Header
    static const uint16_t BLOCK1_SIZE = 10;     // Size of Block 1 excluding CRC (if applicable)
    static const uint16_t CRC_POLY = 0x3D65;
//    static const uint16_t CRC_POLY = 0x4D79;
    uint8_t bytes[300];
    unsigned byte_offset = 0;

///////////////
    if (debug_output > 1) {
        fprintf(stderr,"Debug Wireless M-bus:\n");
        bitbuffer_print(bitbuffer);
    }
/////////////

    // Validate package
    if (bitbuffer->bits_per_row[0] < 100 || bitbuffer->bits_per_row[0] > 700) {  // XXXXXXX
        return 0;
    }

    // Get time now
    local_time_str(0, time_str);

    // Find a Mode T or C data package
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, HEADER_T, sizeof(HEADER_T)*8);
    if (bit_offset + 32*8 >= bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        if (debug_output) { fprintf(stderr, "M-Bus: short package. Header index: %u\n", bit_offset); }
        return 0;
    }
    bit_offset += sizeof(HEADER_T)*8; // skip header

    uint8_t next_byte = bitrow_get_byte(bitbuffer->bb[0], bit_offset);
    if (next_byte == 0x54) {  // Mode C
        bit_offset += 8;
        next_byte = bitrow_get_byte(bitbuffer->bb[0], bit_offset);
        if (next_byte == 0xCD) { // Format A
            if (debug_output) { fprintf(stderr, "M-Bus: Mode C, Format A - not implemented\n"); }
            return 1;
        }
        else if (next_byte == 0x3D) { // Format B
            if (debug_output) { fprintf(stderr, "M-Bus: Mode C, Format B\n"); }
            bit_offset += 8;
            bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, bytes, 8);    // Get length to fetch, including CRCs 
            unsigned field_L = bytes[0];
            // Check length of package is sufficient...
            if (field_L < 12 || field_L+1 > (bitbuffer->bits_per_row[0]-bit_offset)/8) {
                if (debug_output) { fprintf(stderr, "M-Bus, Format B, Package too short for Length!\n"); }
                return 0;
            }
            bitbuffer_extract_bytes(bitbuffer, 0, bit_offset+8, bytes+1, field_L*8);   // Get all the rest..

            // Validate CRC
            unsigned crc_offset = field_L-1;
            uint16_t crc_calc = ~crc16_ccitt(bytes, crc_offset, CRC_POLY, 0);
            uint16_t crc_read = (((uint16_t)bytes[crc_offset] << 8) | bytes[crc_offset+1]);
            if (crc_calc != crc_read) {
                if (debug_output) { fprintf(stderr, "M-Bus: CRC error: CRC 0x%0X, 0x%0X\n", (unsigned)crc_calc, (unsigned)crc_read); }
                return 0;
            }


    //    if (debug_output) {
            char raw_str[1024];
            for (unsigned n=0; n<(field_L+1); n++) { sprintf(raw_str+n*3, "%02x ", bytes[n]); }
            fprintf(stderr, "Raw: %s\n", raw_str);
    //    }

            // Decode Manufacturer
            char m_str[4];
            m_bus_manuf_decode((bytes[3] << 8 | bytes[2]), m_str);
            fprintf(stderr, "Manuf: %s\n", m_str);

            // Decode Manufacturer
            char a_str[4];
            fprintf(stderr, "Address: %0X%0X%0X%0X%0X%0X\n", bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9]);


            return 1;
        }
        else {
            if (debug_output) { fprintf(stderr, "M-Bus: Mode C, Unknown format: 0x%X\n", next_byte); }
            return 0;
        }
    }

    if (debug_output) { fprintf(stderr, "M-Bus: Mode T?\n"); }
    return 0;
    // Test for Mode T...
    /*
        unsigned block_len = 12;   // Block 1 is always 12 bytes long

        // Get Block 1
        if(m_bus_decode_3of6_buffer(bitbuffer->bb[0], bit_offset, bytes, block_len) < 0) {
            if (debug_output) fprintf(stderr, "M-Bus: Decoding error\n");
            return 0;
        }
        bit_offset += block_len * 12;   // 12 bits per byte due to "3of6" coding


        uint8_t bytes_left = bytes[0] - 9;  // Read length field and subtract bytes from block 1 (excl. Length and CRC)
        byte_offset += block_len-2;         // Increment, but leave out CRC bytes
    */


    // Output data
    data = data_make(
        "time",     "",     DATA_STRING,    time_str,
        "model",        "",     DATA_STRING,    "Wireless M-Bus Mode-C2",
//          "id",       "ID",       DATA_INT,   id,
//          "temperature_C",    "Temperature",  DATA_FORMAT,    "%.2f C", DATA_DOUBLE, temp_meas,
//          "setpoint_C",   "Setpoint", DATA_FORMAT,    "%.2f C", DATA_DOUBLE, temp_setp,
//          "switch",       "Switch",   DATA_STRING,    str_sw,
//            "mic",           "Integrity",            DATA_STRING,    "CRC",
        NULL);
    data_acquired_handler(data);

    return 1;
}

/*
static char *output_fields[] = {
    "time",
    "brand"
    "model"
    "id"
    "temperature_C",
    "setpoint_C",
    "switch",
    "mic",
    NULL
};
*/

r_device m_bus_100kbps = {
    .name           = "Wireless M-Bus 100kbps (-f 868950000 -s 1200000)",     // Minimum samplerate = 1.2 MHz (12 samples of 100kb/s)
    .modulation     = FSK_PULSE_PCM,
    .short_limit    = 10,   // Bit rate: 100 kb/s
    .long_limit     = 10,   // NRZ encoding (bit width = pulse width)
    .reset_limit    = 500,  //
    .json_callback  = &m_bus_callback,
    .disabled       = 1,    // Disable per default, as it runs on non-standard frequency
    .demod_arg      = 0,
//    .fields         = output_fields
};
