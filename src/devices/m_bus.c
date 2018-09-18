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
static unsigned bcd2int(uint8_t bcd) {
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


// Data structure for block 1
typedef struct {
    uint8_t     L;        // Length
    uint8_t     C;        // Control
    char        M_str[4]; // Manufacturer (encoded as 2 bytes)
    uint32_t    A_ID;     // Address, ID
    uint8_t     A_Version;    // Address, Version
    uint8_t     A_DevType;    // Address, Device Type
    uint16_t    CRC;      // Optional (Only for Format A)
} m_bus_block1_t;

typedef struct {
    unsigned    length;
    uint8_t     data[512];
} m_bus_data_t;


static int m_bus_decode_format_a(const m_bus_data_t *in, m_bus_data_t *out, m_bus_block1_t *block1) {
    static const uint16_t BLOCK1A_SIZE = 12;     // Size of Block 1, format A

    // Get Block 1
    block1->L         = in->data[0];
    block1->C         = in->data[1];
    m_bus_manuf_decode((uint32_t)(in->data[3] << 8 | in->data[2]), block1->M_str);    // Decode Manufacturer
    block1->A_ID      = bcd2int(in->data[7])*1000000 + bcd2int(in->data[6])*10000 + bcd2int(in->data[5])*100 + bcd2int(in->data[4]);
    block1->A_Version = in->data[8];
    block1->A_DevType = in->data[9];

    // Store length of data
    out->length      = block1->L-9;

    // Validate CRC
    if (!m_bus_crc_valid(in->data, 10)) return 0;

    // Check length of package is sufficient
    unsigned num_data_blocks = (block1->L-9+15)/16;      // Data blocks are 16 bytes long + 2 CRC bytes (not counted in L)
    if ((block1->L < 9) || ((block1->L-9)+num_data_blocks*2 > in->length-BLOCK1A_SIZE)) {   // add CRC bytes for each data block
        if (debug_output) { fprintf(stderr, "M-Bus: Package too short for Length: %u\n", block1->L); }
        return 0;
    }

    // Get all remaining data blocks and concatenate into data array (removing CRC bytes)
    for (unsigned n=0; n < num_data_blocks; ++n) {
        const uint8_t *in_ptr   = in->data+BLOCK1A_SIZE+n*18;       // Pointer to where data starts. Each block is 18 bytes
        uint8_t *out_ptr        = out->data+n*16;                   // Pointer into block where data starts.
        uint8_t block_size      = min(block1->L-9-n*16, 16)+2;      // Maximum block size is 16 Data + 2 CRC

        // Validate CRC
        if (!m_bus_crc_valid(in_ptr, block_size-2)) return 0;

        // Get block data
        memcpy(out_ptr, in_ptr, block_size);
    }
    return 1;
}


static int m_bus_decode_format_b(const m_bus_data_t *in, m_bus_data_t *out, m_bus_block1_t *block1) {
    static const uint16_t BLOCK1B_SIZE  = 10;   // Size of Block 1, format B
    static const uint16_t BLOCK2B_SIZE  = 118;  // Maximum size of Block 2, format B
    static const uint16_t BLOCK1_2B_SIZE  = 128;

    // Get Block 1
    block1->L         = in->data[0];
    block1->C         = in->data[1];
    m_bus_manuf_decode((uint32_t)(in->data[3] << 8 | in->data[2]), block1->M_str);    // Decode Manufacturer
    block1->A_ID      = bcd2int(in->data[7])*1000000 + bcd2int(in->data[6])*10000 + bcd2int(in->data[5])*100 + bcd2int(in->data[4]);
    block1->A_Version = in->data[8];
    block1->A_DevType = in->data[9];

    // Store length of data
    out->length      = block1->L-(9+2);     // Subtract Block 1 and CRC bytes (but include CI)

    // Check length of package is sufficient
    if ((block1->L < 12) || (block1->L+1 > (int)in->length)) {   // L includes all bytes except itself
        if (debug_output) { fprintf(stderr, "M-Bus: Package too short for Length: %u\n", block1->L); }
        return 0;
    }

    // Validate CRC
    if (!m_bus_crc_valid(in->data, min(block1->L-1, (BLOCK1B_SIZE+BLOCK2B_SIZE)-2))) return 0;

    // Get data from Block 2
    memcpy(out->data, in->data+BLOCK1B_SIZE, (min(block1->L-11, BLOCK2B_SIZE-2)));

    // Extract extra block for long telegrams (not tested!)
    uint8_t L_OFFSET = BLOCK1B_SIZE+BLOCK2B_SIZE-1;     // How much to subtract from L (127)
    if (block1->L > (L_OFFSET+2)) {        // Any more data? (besided 2 extra CRC)
        // Validate CRC
        if (!m_bus_crc_valid(in->data+BLOCK1B_SIZE+BLOCK2B_SIZE, block1->L-L_OFFSET-2)) return 0;

        // Get Block 3
        memcpy(out->data+(BLOCK2B_SIZE-2), in->data+BLOCK1B_SIZE+BLOCK2B_SIZE, block1->L-L_OFFSET-2);

        out->length -= 2;   // Subtract the two extra CRC bytes
    }
    return 1;
}


static void m_bus_output_data(const m_bus_data_t *out, const m_bus_block1_t *block1) {
    data_t  *data;
    char    time_str[LOCAL_TIME_BUFLEN];
    char    str_buf[1024];

    // Get time now
    local_time_str(0, time_str);

    // Make data string
    str_buf[0] = 0;
    for (unsigned n=0; n<out->length; n++) { sprintf(str_buf+n*2, "%02x", out->data[n]); }

    // Output data
    data = data_make(
        "time",     "",             DATA_STRING,    time_str,
        "model",    "",             DATA_STRING,    "Wireless M-Bus",
        "M",        "Manufacturer", DATA_STRING,    block1->M_str,
        "id",       "ID",           DATA_INT,       block1->A_ID,
        "version",  "Version",      DATA_INT,       block1->A_Version,
        "type",     "Device Type",  DATA_FORMAT,    "0x%02X",   DATA_INT, block1->A_DevType,
        "type_string",  "Device Type String",   DATA_STRING,        m_bus_device_type_str(block1->A_DevType),
        "C",        "Control",      DATA_FORMAT,    "0x%02X",   DATA_INT, block1->C,
//        "L",        "Length",       DATA_INT,       block1->L,
        "data_length",  "Data Length",          DATA_INT,           out->length,
        "data",     "Data",         DATA_STRING,    str_buf,
        "mic",      "Integrity",    DATA_STRING,    "CRC",
        NULL);
    data_acquired_handler(data);
}


static int m_bus_mode_c_t_callback(bitbuffer_t *bitbuffer) {
    static const uint8_t PREAMBLE_T[]  = {0x55, 0x54, 0x3D};      // Mode T Preamble (always format A - 3of6 encoded)
//  static const uint8_t PREAMBLE_CA[] = {0x55, 0x54, 0x3D, 0x54, 0xCD};  // Mode C, format A Preamble
//  static const uint8_t PREAMBLE_CB[] = {0x55, 0x54, 0x3D, 0x54, 0x3D};  // Mode C, format B Preamble

    m_bus_data_t    data_in     = {0};  // Data from Physical layer decoded to bytes
    m_bus_data_t    data_out    = {0};  // Data from Data Link layer
    m_bus_block1_t  block1      = {0};  // Block1 fields from Data Link layer

    // Validate package length
    if (bitbuffer->bits_per_row[0] < (32+13*8) || bitbuffer->bits_per_row[0] > (64+256*8)) {  // Min/Max (Preamble + payload) 
        return 0;
    }

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
            // Extract data
            data_in.length = (bitbuffer->bits_per_row[0]-bit_offset)/8;
            bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, data_in.data, data_in.length*8);
            // Decode
            if(!m_bus_decode_format_a(&data_in, &data_out, &block1))    return 0;
        } // Format A
        // Format B
        else if (next_byte == 0x3D) {
            if (debug_output) { fprintf(stderr, "M-Bus: Mode C, Format B\n"); }
            // Extract data
            data_in.length = (bitbuffer->bits_per_row[0]-bit_offset)/8;
            bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, data_in.data, data_in.length*8);
            // Decode
            if(!m_bus_decode_format_b(&data_in, &data_out, &block1))    return 0;
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
        if (debug_output) { fprintf(stderr, "M-Bus: Mode T\n"); }
        if (debug_output) { fprintf(stderr, "Experimental - Not tested\n"); }
        // Extract data
        data_in.length = (bitbuffer->bits_per_row[0]-bit_offset)/12;    // Each byte is encoded into 12 bits
        if(m_bus_decode_3of6_buffer(bitbuffer->bb[0], bit_offset, data_in.data, data_in.length) < 0) {
            if (debug_output) fprintf(stderr, "M-Bus: Decoding error\n");
            return 0;
        }
        // Decode
        if(!m_bus_decode_format_a(&data_in, &data_out, &block1))    return 0;
    }   // Mode T

    m_bus_output_data(&data_out, &block1);
    return 1;
}


static int m_bus_mode_r_callback(bitbuffer_t *bitbuffer) {
    static const uint8_t PREAMBLE_RA[]  = {0x55, 0x54, 0x76, 0x96};      // Mode R, format A (B not supported)

    m_bus_data_t    data_in     = {0};  // Data from Physical layer decoded to bytes
    m_bus_data_t    data_out    = {0};  // Data from Data Link layer
    m_bus_block1_t  block1      = {0};  // Block1 fields from Data Link layer

    // Validate package length
    if (bitbuffer->bits_per_row[0] < (32+13*8) || bitbuffer->bits_per_row[0] > (64+256*8)) {  // Min/Max (Preamble + payload) 
        return 0;
    }

    // Find a data package
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, PREAMBLE_RA, sizeof(PREAMBLE_RA)*8);
    if (bit_offset + 13*8 >= bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        return 0;
    }
    bit_offset += sizeof(PREAMBLE_RA)*8;     // skip preamble

    if (debug_output) { fprintf(stderr, "M-Bus: Mode R, Format A\n"); }
    if (debug_output) { fprintf(stderr, "Experimental - Not tested\n"); }
    // Extract data
    data_in.length = (bitbuffer->bits_per_row[0]-bit_offset)/8;
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, data_in.data, data_in.length*8);
    // Decode
    if(!m_bus_decode_format_a(&data_in, &data_out, &block1))    return 0;

    m_bus_output_data(&data_out, &block1);
    return 1;
}


static int m_bus_mode_f_callback(bitbuffer_t *bitbuffer) {
    static const uint8_t PREAMBLE_F[]  = {0x55, 0xF6};      // Mode F Preamble
//  static const uint8_t PREAMBLE_FA[] = {0x55, 0xF6, 0x8D};  // Mode F, format A Preamble
//  static const uint8_t PREAMBLE_FB[] = {0x55, 0xF6, 0x72};  // Mode F, format B Preamble

    m_bus_data_t    data_in     = {0};  // Data from Physical layer decoded to bytes
    m_bus_data_t    data_out    = {0};  // Data from Data Link layer
    m_bus_block1_t  block1      = {0};  // Block1 fields from Data Link layer

    // Validate package length
    if (bitbuffer->bits_per_row[0] < (32+13*8) || bitbuffer->bits_per_row[0] > (64+256*8)) {  // Min/Max (Preamble + payload) 
        return 0;
    }

    // Find a Mode F data package
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, PREAMBLE_F, sizeof(PREAMBLE_F)*8);
    if (bit_offset + 13*8 >= bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        return 0;
    }
    bit_offset += sizeof(PREAMBLE_F)*8;     // skip preamble

    uint8_t next_byte = bitrow_get_byte(bitbuffer->bb[0], bit_offset);
    bit_offset += 8;
    // Format A
    if (next_byte == 0x8D) {
        if (debug_output) { fprintf(stderr, "M-Bus: Mode F, Format A\n"); }
        if (debug_output) { fprintf(stderr, "Not implemented\n"); }
        return 1;
    } // Format A
    // Format B
    else if (next_byte == 0x72) {
        if (debug_output) { fprintf(stderr, "M-Bus: Mode F, Format B\n"); }
        if (debug_output) { fprintf(stderr, "Not implemented\n"); }
        return 1;
    }   // Format B
    // Unknown Format
    else {
        if (debug_output) {
            fprintf(stderr, "M-Bus: Mode F, Unknown format: 0x%X\n", next_byte);
            bitbuffer_print(bitbuffer);
        }
        return 0;
    }

    m_bus_output_data(&data_out, &block1);
    return 1;
}


// Mode C1, C2 (Meter TX), T1, T2 (Meter TX),
// Frequency 868.95 MHz, Bitrate 100 kbps, Modulation NRZ FSK
r_device m_bus_mode_c_t = {
    .name           = "Wireless M-Bus, Mode C&T, 100kbps (-f 868950000 -s 1200000)",     // Minimum samplerate = 1.2 MHz (12 samples of 100kb/s)
    .modulation     = FSK_PULSE_PCM,
    .short_limit    = 10,   // Bit rate: 100 kb/s
    .long_limit     = 10,   // NRZ encoding (bit width = pulse width)
    .reset_limit    = 500,  //
    .json_callback  = &m_bus_mode_c_t_callback,
    .disabled       = 1,    // Disable per default, as it runs on non-standard frequency
    .demod_arg      = 0,
};


// Mode S1, S1-m, S2, T2 (Meter RX),    (Meter RX not so interesting)
// Frequency 868.3 MHz, Bitrate 32.768 kbps, Modulation Manchester FSK
// Untested!!! (Need samples)
r_device m_bus_mode_s = {
    .name           = "Wireless M-Bus, Mode S, 32.768kbps (-f 868300000 -s 1000000)",   // Minimum samplerate = 1 MHz (15 samples of 32kb/s manchester coded)
    .modulation     = FSK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit    = (1000.0/32.768/2),   // ~31 us per bit -> clock half period ~15 us
    .long_limit     = 0,    // Unused
    .reset_limit    = (1000.0/32.768*1.5), // 3 clock half periods
    .json_callback  = &m_bus_mode_c_t_callback,
    .disabled       = 1,    // Disable per default, as it runs on non-standard frequency
    .demod_arg      = 0,
};


// Mode C2 (Meter RX)
// Frequency 869.525 MHz, Bitrate 50 kbps, Modulation Manchester
//      Note: Not so interesting, as it is only Meter RX


// Mode R2
// Frequency 868.33 MHz, Bitrate 4.8 kbps, Modulation Manchester FSK
//      Preamble {0x55, 0x54, 0x76, 0x96} (Format A) (B not supported)
// Untested stub!!! (Need samples)
r_device m_bus_mode_r = {
    .name           = "Wireless M-Bus, Mode R, 4.8kbps (-f 868330000)",
    .modulation     = FSK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit    = (1000.0/4.8/2),   // ~208 us per bit -> clock half period ~104 us
    .long_limit     = 0,    // Unused
    .reset_limit    = (1000.0/4.8*1.5), // 3 clock half periods
    .json_callback  = &m_bus_mode_r_callback,
    .disabled       = 1,    // Disable per default, as it runs on non-standard frequency
    .demod_arg      = 0,
};

// Mode N
// Frequency 169.400 MHz to 169.475 MHz in 12.5/25/50 kHz bands
// Bitrate 2.4/4.8 kbps, Modulation GFSK,
//      Preamble {0x55, 0xF6, 0x8D} (Format A)
//      Preamble {0x55, 0xF6, 0x72} (Format B)
//      Note: FDMA currently not supported, but Mode F2 may be usable for 2.4
// Bitrate 19.2 kbps, Modulation 4 GFSK (9600 BAUD)
//      Note: Not currently possible with rtl_433


// Mode F2
// Frequency 433.82 MHz, Bitrate 2.4 kbps, Modulation NRZ FSK
//      Preamble {0x55, 0xF6, 0x8D} (Format A)
//      Preamble {0x55, 0xF6, 0x72} (Format B)
// Untested stub!!! (Need samples)
r_device m_bus_mode_f = {
    .name           = "Wireless M-Bus, Mode F, 2.4kbps",
    .modulation     = FSK_PULSE_PCM,
    .short_limit    = 1000.0/2.4,   // ~417 us
    .long_limit     = 1000.0/2.4,   // NRZ encoding (bit width = pulse width)
    .reset_limit    = 5000,         // ??
    .json_callback  = &m_bus_mode_f_callback,
    .disabled       = 1,    // Disable per default, as it runs on non-standard frequency
    .demod_arg      = 0,
};
