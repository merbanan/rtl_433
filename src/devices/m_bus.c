/* Wireless M-Bus (EN 13757-4)
 *
 * Implements the Physical layer (RF receiver) and Data Link layer of the
 * Wireless M-Bus protocol. Will return a data string (including the CI byte)
 * for further processing by an Application layer (outside this program).
 *
 * Copyright (C) 2018 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "util.h"

// Convert two BCD encoded nibbles to an integer
inline unsigned bcd2int(uint8_t bcd) {
    return 10*(bcd>>4) + (bcd & 0xF);
}

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


// Validate CRC
static int m_bus_crc_valid(const uint8_t* bytes, unsigned crc_offset) {
    static const uint16_t CRC_POLY = 0x3D65;
    uint16_t crc_calc = ~crc16_ccitt(bytes, crc_offset, CRC_POLY, 0);
    uint16_t crc_read = (((uint16_t)bytes[crc_offset] << 8) | bytes[crc_offset+1]);
    if (crc_calc != crc_read) {
        if (debug_output) {
            fprintf(stderr, "M-Bus: CRC error: Calculated 0x%0X, Read: 0x%0X\n", (unsigned)crc_calc, (unsigned)crc_read);
        }
        return 0;
    }
    return 1;
}


// Decode two bytes into three letters of five bits
static void m_bus_manuf_decode(uint16_t m_field, char* three_letter_code) {
    three_letter_code[0] = (m_field >> 10 & 0x1F) + 0x40;
    three_letter_code[1] = (m_field >> 5 & 0x1F) + 0x40;
    three_letter_code[2] = (m_field & 0x1F) + 0x40;
    three_letter_code[3] = 0;
}


// Decode device type string
const char* m_bus_device_type_str(uint8_t devType) {
    char *str = "";
    switch(devType) {
        case 0x00:  str = "Other";  break;
        case 0x01:  str = "Oil";  break;
        case 0x02:  str = "Electricity";  break;
        case 0x03:  str = "Gas";  break;
        case 0x04:  str = "Heat";  break;
        case 0x05:  str = "Steam";  break;
        case 0x06:  str = "Warm Water";  break;
        case 0x07:  str = "Water";  break;
        case 0x08:  str = "Heat Cost Allocator";  break;
        case 0x09:  str = "Compressed Air";  break;
        case 0x0A:
        case 0x0B:  str = "Cooling load meter";  break;
        case 0x0C:  str = "Heat";  break;
        case 0x0D:  str = "Heat/Cooling load meter";  break;
        case 0x0E:  str = "Bus/System component";  break;
        case 0x0F:  str = "Unknown";  break;
        case 0x15:  str = "Hot Water";  break;
        case 0x16:  str = "Cold Water";  break;
        case 0x17:  str = "Hot/Cold Water meter";  break;
        case 0x18:  str = "Pressure";  break;
        case 0x19:  str = "A/D Converter";  break;
        default:    break;  // Unknown
    }
    return str;
}


static int m_bus_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    static const uint8_t PREAMBLE_T[]  = { 0x55, 0x54, 0x3D};      // Mode T Preamble (always format A - 3of6 encoded)
//  static const uint8_t PREAMBLE_CA[] = { 0x55, 0x54, 0x3D, 0x54, 0xCD};  // Mode C, format A Preamble
//  static const uint8_t PREAMBLE_CB[] = { 0x55, 0x54, 0x3D, 0x54, 0x3D};  // Mode C, format B Preamble
    static const uint16_t BLOCK1A_SIZE = 12;     // Size of Block 1, format A
    static const uint16_t BLOCK1B_SIZE = 10;     // Size of Block 1, format B

    uint8_t     block[300];     // Concatenated blocks with CRC fields removed from data. Maximum Length: 1+255+17*2 (Format A)
    uint8_t     field_L;        // Length
    uint8_t     field_C;        // Control
    char        field_M_str[4]; // Manufacturer
    uint32_t    field_A_ID;     // Address, ID
    uint8_t     field_A_Version;    // Address, Version
    uint8_t     field_A_DevType;    // Address, Device Type
    uint16_t    field_CRC;
    uint8_t     data_offset;    // Start of data bytes in block for Application Layer
    uint8_t     data_length;    // Length of data bytes
    char        str_buf[1024];

    // Validate package length
    if (bitbuffer->bits_per_row[0] < (32+13*8) || bitbuffer->bits_per_row[0] > (64+256*8)) {  // Min/Max (Preamble + payload) 
        return 0;
    }

    // Get time now
    local_time_str(0, time_str);

    // Find a Mode T or C data package
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, PREAMBLE_T, sizeof(PREAMBLE_T)*8);
    if (bit_offset + 13*8 >= bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        return 0;
    }
    bit_offset += sizeof(PREAMBLE_T)*8;     // skip preamble

    uint8_t next_byte = bitrow_get_byte(bitbuffer->bb[0], bit_offset);
    bit_offset += 8;
    // Mode C
    if (next_byte == 0x54) {
        next_byte = bitrow_get_byte(bitbuffer->bb[0], bit_offset);
        bit_offset += 8;
        // Format A
        if (next_byte == 0xCD) {
            if (debug_output) { fprintf(stderr, "M-Bus: Mode C, Format A\n"); }
            // Get Block 1
            bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, block, BLOCK1A_SIZE*8);
            bit_offset += BLOCK1A_SIZE*8;
            field_L         = block[0];
            field_C         = block[1];
            m_bus_manuf_decode((uint32_t)(block[3] << 8 | block[2]), field_M_str);    // Decode Manufacturer
            field_A_ID      = bcd2int(block[7])*1000000 + bcd2int(block[6])*10000 + bcd2int(block[5])*100 + bcd2int(block[4]);
            field_A_Version = block[8];
            field_A_DevType = block[9];
            data_offset     = BLOCK1A_SIZE;
            data_length     = field_L-9;     // Store length of data

            // Validate CRC
            if (!m_bus_crc_valid(block, 10)) return 0;

            // Check length of package is sufficient...
            if ((field_L < 9) || ((field_L-9)*8 > (int)(bitbuffer->bits_per_row[0]-bit_offset))) {
                if (debug_output) { fprintf(stderr, "M-Bus: Package too short for Length: %u\n", field_L); }
                return 0;
            }

            // Get all remaining blocks (2...) and concatenate into block array (overwriting CRC bytes)
            for (int n=0; n < (field_L-9+15)/16; ++n) {
                uint8_t *block_ptr = block+BLOCK1A_SIZE+n*16;       // Pointer into block where data starts. Skip the 2 CRC bytes!
                uint8_t block_size = min(field_L-9-n*16, 16)+2;     // Maximum block size is 16 Data + 2 CRC

                // Get Block 2+n
                bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, block_ptr, block_size*8);
                bit_offset += block_size*8;

                // Validate CRC
                if (!m_bus_crc_valid(block_ptr, block_size-2)) return 0;
            }
        } // Format A
        // Format B
        else if (next_byte == 0x3D) {
            if (debug_output) { fprintf(stderr, "M-Bus: Mode C, Format B\n"); }
            // Get Block 1 and decode
            bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, block, BLOCK1B_SIZE*8);
            bit_offset += BLOCK1B_SIZE*8;
            field_L         = block[0];
            field_C         = block[1];
            m_bus_manuf_decode((uint32_t)(block[3] << 8 | block[2]), field_M_str);    // Decode Manufacturer
            field_A_ID      = bcd2int(block[7])*1000000 + bcd2int(block[6])*10000 + bcd2int(block[5])*100 + bcd2int(block[4]);
            field_A_Version = block[8];
            field_A_DevType = block[9];
            data_offset     = BLOCK1B_SIZE;
            data_length     = field_L-(BLOCK1B_SIZE-1)-2;     // Subtract CRC bytes (but include CI)

            // Check length of package is sufficient...
            if ((field_L < 12) || (((field_L-(BLOCK1B_SIZE-1)))*8 > (int)(bitbuffer->bits_per_row[0]-bit_offset))) {
                if (debug_output) { fprintf(stderr, "M-Bus: Package too short for Length: %u\n", field_L); }
                return 0;
            }
            // Get Block 2
            bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, block+BLOCK1B_SIZE, (min(field_L, 127)-9)*8);  // Cap length for long telegrams
            bit_offset += (min(field_L, 127)-9)*8;

            // Validate CRC
            if (!m_bus_crc_valid(block, min(field_L, 127)-1)) return 0;

            // Extract extra block for long telegrams (not tested!)
            if (field_L >= (128-1+3)) {
                // Get Block 3
                unsigned block_offset = 126;     // Where to start new block (128-2 to overwrite CRC)
                bit_offset += 128*8;
                bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, block+block_offset, (field_L-127)*8);

                // Validate CRC
                if (!m_bus_crc_valid(block+block_offset, field_L-127-2)) return 0;

                data_length -= 2;   // Subtract the two extra CRC bytes
            }
        }   // Format B
        // Unknown Format
        else {
            if (debug_output) {
                fprintf(stderr, "M-Bus: Mode C, Unknown format: 0x%X\n", next_byte);
                bitbuffer_print(bitbuffer);
            }
            return 0;
        }
    }   // Mode C
    // Mode T
    else {
        if (debug_output) {
            fprintf(stderr, "M-Bus: Mode T - Not implemented\n");
            bitbuffer_print(bitbuffer);
        }
        return 0;
        /*
            // Get Block 1
            if(m_bus_decode_3of6_buffer(bitbuffer->bb[0], bit_offset, block, BLOCK1A_SIZE)) < 0) {
                if (debug_output) fprintf(stderr, "M-Bus: Decoding error\n");
                return 0;
            }
            bit_offset += BLOCK1A_SIZE * 12;   // 12 bits per byte due to "3of6" coding
        */
    }   // Mode T

    // Make data string
    str_buf[0] = 0;
    for (unsigned n=0; n<data_length; n++) { sprintf(str_buf+n*2, "%02x", block[n+data_offset]); }

    // Output data
    data = data_make(
        "time",     "",             DATA_STRING,    time_str,
        "model",    "",             DATA_STRING,    "Wireless M-Bus",
        "M",        "Manufacturer", DATA_STRING,    field_M_str,
        "id",       "ID",           DATA_INT,       field_A_ID,
        "version",  "Version",      DATA_INT,       field_A_Version,
        "type",     "Device Type",  DATA_FORMAT,    "0x%02X",   DATA_INT, field_A_DevType,
        "type_string",  "Device Type String",   DATA_STRING,        m_bus_device_type_str(field_A_DevType),
        "C",        "Control",      DATA_FORMAT,    "0x%02X",   DATA_INT, field_C,
        "L",        "Length",       DATA_INT,       field_L,
        "data_length",  "Data Length",          DATA_INT,           data_length,
        "data",     "Data",         DATA_STRING,    str_buf,
        "mic",      "Integrity",    DATA_STRING,    "CRC",
        NULL);
    data_acquired_handler(data);

    return 1;
}


r_device m_bus_100kbps = {
    .name           = "Wireless M-Bus 100kbps (-f 868950000 -s 1200000)",     // Minimum samplerate = 1.2 MHz (12 samples of 100kb/s)
    .modulation     = FSK_PULSE_PCM,
    .short_limit    = 10,   // Bit rate: 100 kb/s
    .long_limit     = 10,   // NRZ encoding (bit width = pulse width)
    .reset_limit    = 500,  //
    .json_callback  = &m_bus_callback,
    .disabled       = 1,    // Disable per default, as it runs on non-standard frequency
    .demod_arg      = 0,
};
