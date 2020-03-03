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
#include "decoder.h"

#define BLOCK1A_SIZE 12     // Size of Block 1, format A
#define BLOCK1B_SIZE 10     // Size of Block 1, format B
#define BLOCK2B_SIZE 118    // Maximum size of Block 2, format B
#define BLOCK1_2B_SIZE 128

// Convert two BCD encoded nibbles to an integer
static unsigned bcd2int(uint8_t bcd) {
    return 10*(bcd>>4) + (bcd & 0xF);
}

// Mapping from 6 bits to 4 bits. "3of6" coding used for Mode T
static uint8_t m_bus_decode_3of6(uint8_t byte) {
    uint8_t out = 0xFF; // Error
    //fprintf(stderr,"Decode %0d\n", byte);
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
// Bad data must be handled with second layer CRC
static int m_bus_decode_3of6_buffer(const bitrow_t bits, unsigned bit_offset, uint8_t* output, unsigned num_bytes) {
    for (unsigned n=0; n<num_bytes; ++n) {
        uint8_t nibble_h = m_bus_decode_3of6(bitrow_get_byte(bits, n*12+bit_offset) >> 2);
        uint8_t nibble_l = m_bus_decode_3of6(bitrow_get_byte(bits, n*12+bit_offset+6) >> 2);
        output[n] = (nibble_h << 4) | nibble_l;
    }
    return 0;
}


// Validate CRC
static int m_bus_crc_valid(r_device *decoder, const uint8_t *bytes, unsigned crc_offset)
{
    static const uint16_t CRC_POLY = 0x3D65;
    uint16_t crc_calc = ~crc16(bytes, crc_offset, CRC_POLY, 0);
    uint16_t crc_read = (((uint16_t)bytes[crc_offset] << 8) | bytes[crc_offset+1]);
    if (crc_calc != crc_read) {
        if (decoder->verbose) {
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
        case 0x1A:  str = "Smoke detector"; break;
        case 0x1B:  str = "Room sensor"; break;
        case 0x1C:  str = "Gas detector"; break;
        case 0x20:  str = "Breaker (electricity)"; break;
        case 0x21:  str = "Valve (gas or water)"; break;
        case 0x28:  str = "Waste water meter"; break;
        case 0x29:  str = "Garbage"; break;
        case 0x2A:  str = "Carbon dioxide"; break;
        case 0x25:  str = "Customer unit (display device)";break;
        case 0x31:  str = "Communication controller";break;
        case 0x32:  str = "Unidirectional repeater";break;
        case 0x33:  str = "Bidirectional repeater";break;
        case 0x36:  str = "Radio converter (system side)";break;
        case 0x37:  str = "Radio converter (meter side)";break;
        default:    break;  // Unknown
    }
    return str;
}


// Data structure for application layer
typedef struct {
    uint8_t     CI;         // Control info
    uint8_t     AC;         // Access number
    uint8_t     ST;
    uint16_t    CW;         // Configuration word
    uint8_t     pl_offset;  // Payload offset
    /* KNX */
    uint8_t     knx_ctrl;
    uint16_t    src;
    uint16_t    dst;
    uint8_t     l_npci;
    uint8_t     tpci;
    uint8_t     apci;
} m_bus_block2_t;

// Data structure for block 1
typedef struct {
    uint8_t     L;        // Length
    uint8_t     C;        // Control
    char        M_str[4]; // Manufacturer (encoded as 2 bytes)
    uint32_t    A_ID;     // Address, ID
    uint8_t     A_Version;    // Address, Version
    uint8_t     A_DevType;    // Address, Device Type
    uint16_t    CRC;      // Optional (Only for Format A)
    m_bus_block2_t block2;
    int         knx_mode;
    uint8_t     knx_sn[6];
} m_bus_block1_t;

typedef struct {
    unsigned    length;
    uint8_t     data[512];
} m_bus_data_t;


static float record_factor[4] = { 0.001, 0.01, 0.1, 1 };
static float humidity_factor[2] = { 0.1, 1 };

static int consumed_bytes[8] = { -1, 1, 2, 3, 4, 4, 6, 8};

static char* oms_temp[4][4] = {
{"temperature_C","average_temperature_1h_C","average_temperature_24h_C","error_04", },
{"maximum_temperature_1h_C","maximum_temperature_24h_C","error_13","error_14",},
{"minimum_temperature_1h_C","minimum_temperature_24h_C","error_23","error_24",},
{"error_31","error_32","error_33","error_34",}
};

static char* oms_temp_el[4][4] = {
{"Temperature","Average Temperature 1h","Average Temperature 24h","Error [0][4]", },
{"Maximum Temperature 1h","Maximum Temperature 24h","Error [1][3]","Error [1][4]",},
{"Minimum Temperature 1h","Minimum Temperature 24h","Error [2][3]","Error [2][4]",},
{"error_31","error_32","error_33","error_34",}
};

static char* oms_hum[4][4] = {
{"humidity","average_humidity_1h","average_humidity_24h","error_04", },
{"maximum_humidity_1h","maximum_humidity_24h","error_13","error_14",},
{"minimum_humidity_1h","minimum_humidity_24h","error_23","error_24",},
{"error_31","error_32","error_33","error_34",}
};

static char* oms_hum_el[4][4] = {
{"Humidity","Average Humidity 1h","Average Humidity 24h","Error [0][4]", },
{"Maximum Humidity 1h","Maximum Humidity 24h","Error [1][3]","Error [1][4]",},
{"Minimum Humidity 1h","Minimum Humidity 24h","Error [2][3]","Error [2][4]",},
{"Error 31","Error 32","Error 33","Error 34",}
};

static int m_bus_decode_records(data_t *data, const uint8_t *b, uint8_t dif_coding, uint8_t vif_linear, uint8_t vif_uam, uint8_t dif_sn, uint8_t dif_ff, uint8_t dif_su) {
    int ret = consumed_bytes[dif_coding&0x07];
    float temp;
    int state;

    switch (vif_linear) {
        case 0:
            switch(vif_uam>>2) {
                case 0x19:
                    temp = (int16_t)((b[1]<<8)|b[0])*record_factor[vif_uam&0x3];
                    data = data_append(data,
                        oms_temp[dif_ff&0x3][dif_sn&0x3], oms_temp_el[dif_ff&0x3][dif_sn&0x3], DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp,
                        NULL);
                    break;
                default:
                    break;
            }
            break;
        case 0x7B:
            switch(vif_uam>>1) {
                case 0xD:
                    data = data_append(data, oms_hum[dif_ff&0x3][dif_sn&0x3], oms_hum_el[dif_ff&0x3][dif_sn&0x3], DATA_FORMAT, "%.1f %%", DATA_DOUBLE, b[0]*humidity_factor[vif_uam&0x1], NULL);
                    break;
                default:
                    break;
            }
            break;
        case 0x7D:
            switch(vif_uam) {
                case 0x1b:
                    // If tamper is triggered the bit 0 and 4 is set
                    // Open  sets bits 2 and 6 to 1
                    // Close sets bits 2 and 6 to 0
                    state = b[0]&0x44;
                    data = data_append(data, "switch", "Switch", DATA_FORMAT, "%s", DATA_STRING, (state==0x44) ? "open":"closed", NULL);
                    break;
                case 0x3a:
                    /* Only use 32 bits of 48 available */
                    data = data_append(data, ((dif_su==0)?"counter_0":"counter_1"), ((dif_su==0)?"Counter 0":"Counter 1"), DATA_FORMAT, "%d", DATA_INT, (b[3]<<24|b[2]<<16|b[1]<<8|b[0]), NULL);
                    break;
                default:
                    break;
            }
        default:
            break;
    }
    return ret;
}

static void parse_payload(data_t *data, const m_bus_block1_t *block1, const m_bus_data_t *out) {
    uint8_t off = block1->block2.pl_offset;
    const uint8_t *b = out->data;
    uint8_t dif = 0;
    uint8_t dife_array[10] = {0};
    uint8_t dife_cnt = 0;
    uint8_t dif_coding = 0;
    uint8_t dif_sn = 0;
    uint8_t dif_ff = 0;
    uint8_t dif_su = 0;
    uint8_t vif = 0;
    uint8_t vife_array[10] = {0};
    uint8_t vife_cnt = 0;
    uint8_t vif_uam = 0;
    uint8_t vif_linear = 0;
    uint8_t vife = 0;
    uint8_t exponent = 0;
    int cnt = 0, consumed;

    /* Align offset pointer, there might be 2 0x2F bytes */
    if (b[off] == 0x2F) off++;
    if (b[off] == 0x2F) off++;

// [02 65] 9f08 [42 65] 9e08 [8201 65] 8f08 [02 fb1a] 3601 [42 fb1a] 3701 [8201 fb1a] 3001

//[02 65] b408 [42 65] a008 [8201 65] 6408 [22 65] 9608 [12 65] ac08 [62 65] 2808 [52 65] 920802fb1a470142fb1a4a018201fb1a550122fb1a4a0112fb1a4a0162fb1a3c0152fb1a6c01066dbb3197902100

    /* Payload must start with a DIF */
    while(off < block1->L) {
        memset(dife_array, 0, 10);
        memset(vife_array, 0, 10);
        dife_cnt = 0;
        vife_cnt = 0;

        /* Parse DIF */
        dif = b[off];
        dif_sn = (dif&0x40) >> 6;
        dif_su = 0;
        while (b[off]&0x80) {
            off++;
            dife_array[dife_cnt++] = b[off];
            if (dife_cnt >= 10) return;
        }
        // Only use first dife in dife_array
        dif_sn = ((dife_array[0]&0x0F) << 1) | dif_sn;
        dif_su = ((dife_array[0]&0x40) >> 6);
        off++;
        dif_coding = dif&0x0F;
        dif_ff = (dif&0x30) >> 4;

        /* Parse VIF */
        vif = b[off];

        while (b[off]&0x80) {
            off++;
            vife_array[vife_cnt++] = b[off]&0x7F;
            if (vife_cnt >= 10) return;
        }
        off++;
        /* Linear VIF-extension */
        if (vif == 0xFB) {
            vif_linear = 0x7B;
            vif_uam = vife_array[0];
        } else if(vif  == 0xFD) {
            vif_linear = 0x7D;
            vif_uam = vife_array[0];
        } else {
            vif_linear = 0;
            vif_uam = vif&0x7F;
        }

        consumed = m_bus_decode_records(data, &b[off], dif_coding, vif_linear, vif_uam, dif_sn, dif_ff, dif_su);
        if (consumed == -1) return;

        off +=consumed;
    }
    return;
}

static int parse_block2(r_device *decoder, const m_bus_data_t *in, m_bus_block1_t *block1) {
    m_bus_block2_t *b2 = &block1->block2;
    const uint8_t *b = in->data+BLOCK1A_SIZE;

    if (block1->knx_mode) {
        b2->knx_ctrl = b[0];
        b2->src = b[1]<< 8 | b[2];
        b2->dst = b[3]<< 8 | b[4];
        b2->l_npci = b[5];
        b2->tpci = b[6];
        b2->apci = b[7];
        /* data */
    } else {
        b2->CI = b[0];
        /* Short transport layer */
        if (b2->CI == 0x7A) {
            b2->AC = b[1];
            b2->ST = b[2];
            b2->CW = b[4]<<8 | b[3];
            b2->pl_offset = BLOCK1A_SIZE-2 + 5;
        }
    //    fprintf(stderr, "Instantaneous Value: %02x%02x : %f\n",b[9],b[10],((b[10]<<8)|b[9])*0.01);
    }
    return 0;
}

static int m_bus_decode_format_a(r_device *decoder, const m_bus_data_t *in, m_bus_data_t *out, m_bus_block1_t *block1)
{

    // Get Block 1
    block1->L         = in->data[0];
    block1->C         = in->data[1];

    /* Check for KNX RF default values */
    if ((in->data[2]==0xFF) && (in->data[3]==0x03)) {
        block1->knx_mode = 1;
        memcpy(block1->knx_sn, &in->data[4], 6);
    } else {
        m_bus_manuf_decode((uint32_t)(in->data[3] << 8 | in->data[2]), block1->M_str);    // Decode Manufacturer
        block1->A_ID      = bcd2int(in->data[7])*1000000 + bcd2int(in->data[6])*10000 + bcd2int(in->data[5])*100 + bcd2int(in->data[4]);
        block1->A_Version = in->data[8];
        block1->A_DevType = in->data[9];
    }

    // Store length of data
    out->length      = block1->L-9 + BLOCK1A_SIZE-2;

    // Validate CRC
    if (!m_bus_crc_valid(decoder, in->data, 10)) return 0;

    // Check length of package is sufficient
    unsigned num_data_blocks = (block1->L-9+15)/16;      // Data blocks are 16 bytes long + 2 CRC bytes (not counted in L)
    if ((block1->L < 9) || ((block1->L-9)+num_data_blocks*2 > in->length-BLOCK1A_SIZE)) {   // add CRC bytes for each data block
        if (decoder->verbose) { fprintf(stderr, "M-Bus: Package too short for Length: %u\n", block1->L); }
        return 0;
    }

    memcpy(out->data, in->data, BLOCK1A_SIZE-2);
    // Get all remaining data blocks and concatenate into data array (removing CRC bytes)
    for (unsigned n=0; n < num_data_blocks; ++n) {
        const uint8_t *in_ptr   = in->data+BLOCK1A_SIZE+n*18;       // Pointer to where data starts. Each block is 18 bytes
        uint8_t *out_ptr        = out->data+n*16 + BLOCK1A_SIZE-2;                   // Pointer into block where data starts.
        uint8_t block_size      = MIN(block1->L-9-n*16, 16)+2;      // Maximum block size is 16 Data + 2 CRC

        // Validate CRC
        if (!m_bus_crc_valid(decoder, in_ptr, block_size-2)) return 0;

        // Get block data
        memcpy(out_ptr, in_ptr, block_size);
    }

    parse_block2(decoder, in, block1);

    return 1;
}

static int m_bus_decode_format_b(r_device *decoder, const m_bus_data_t *in, m_bus_data_t *out, m_bus_block1_t *block1)
{
    // Get Block 1
    block1->L         = in->data[0];
    block1->C         = in->data[1];
    m_bus_manuf_decode((uint32_t)(in->data[3] << 8 | in->data[2]), block1->M_str);    // Decode Manufacturer
    block1->A_ID      = bcd2int(in->data[7])*1000000 + bcd2int(in->data[6])*10000 + bcd2int(in->data[5])*100 + bcd2int(in->data[4]);
    block1->A_Version = in->data[8];
    block1->A_DevType = in->data[9];

    // Store length of data
    out->length      = block1->L-(9+2) + BLOCK1B_SIZE-2;

    // Check length of package is sufficient
    if ((block1->L < 12) || (block1->L+1 > (int)in->length)) {   // L includes all bytes except itself
        if (decoder->verbose) { fprintf(stderr, "M-Bus: Package too short for Length: %u\n", block1->L); }
        return 0;
    }

    // Validate CRC
    if (!m_bus_crc_valid(decoder, in->data, MIN(block1->L-1, (BLOCK1B_SIZE+BLOCK2B_SIZE)-2))) return 0;

    // Get data from Block 2
    memcpy(out->data, in->data, (MIN(block1->L-11, BLOCK2B_SIZE-2))+BLOCK1B_SIZE);

    // Extract extra block for long telegrams (not tested!)
    uint8_t L_OFFSET = BLOCK1B_SIZE+BLOCK2B_SIZE-1;     // How much to subtract from L (127)
    if (block1->L > (L_OFFSET+2)) {        // Any more data? (besided 2 extra CRC)
        // Validate CRC
        if (!m_bus_crc_valid(decoder, in->data+BLOCK1B_SIZE+BLOCK2B_SIZE, block1->L-L_OFFSET-2)) return 0;

        // Get Block 3
        memcpy(out->data+(BLOCK2B_SIZE-2), in->data+BLOCK2B_SIZE, block1->L-L_OFFSET-2);

        out->length -= 2;   // Subtract the two extra CRC bytes
    }
    return 1;
}

static void m_bus_output_data(r_device *decoder, const m_bus_data_t *out, const m_bus_block1_t *block1, const char *mode)
{
    data_t  *data;
    char    str_buf[1024];

    // Get time now

    // Make data string
    str_buf[0] = 0;
    for (unsigned n=0; n<out->length; n++) { sprintf(str_buf+n*2, "%02x", out->data[n]); }

    // Output data
    if (block1->knx_mode) {
        char sn_str[7*2] = {0};
        for (unsigned n=0; n<6; n++) { sprintf(sn_str+n*2, "%02x", block1->knx_sn[n]); }

        data = data_make(
        "model",    "",             DATA_STRING,    _X("KNX-RF","KNX-RF"),
        "sn",       "SN",           DATA_STRING,    sn_str,
        "knx_ctrl", "KNX-Ctrl",     DATA_FORMAT,    "0x%02X", DATA_INT, block1->block2.knx_ctrl,
        "src",      "Src",          DATA_FORMAT,    "0x%04X", DATA_INT, block1->block2.src,
        "dst",      "Dst",          DATA_FORMAT,    "0x%04X", DATA_INT, block1->block2.dst,
        "l_npci",   "L/NPCI",       DATA_FORMAT,    "0x%02X", DATA_INT, block1->block2.l_npci,
        "tpci",     "TPCI",         DATA_FORMAT,    "0x%02X", DATA_INT, block1->block2.tpci,
        "apci",     "APCI",         DATA_FORMAT,    "0x%02X", DATA_INT, block1->block2.apci,
        "data_length","Data Length",DATA_INT,       out->length,
        "data",     "Data",         DATA_STRING,    str_buf,
        "mic",      "Integrity",    DATA_STRING,    "CRC",
        NULL);
    } else {
        data = data_make(
        "model",    "",             DATA_STRING,    _X("Wireless-MBus","Wireless M-Bus"),
        "mode",     "Mode",         DATA_STRING,    mode,
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
    }
    if(block1->block2.CI) {
        data = data_append(data,
        "CI",     "Control Info",   DATA_FORMAT,    "0x%02X",   DATA_INT, block1->block2.CI,
        "AC",     "Access number",  DATA_FORMAT,    "0x%02X",   DATA_INT, block1->block2.AC,
        "ST",     "Device Type",    DATA_FORMAT,    "0x%02X",   DATA_INT, block1->block2.ST,
        "CW",     "Configuration Word",DATA_FORMAT, "0x%04X",   DATA_INT, block1->block2.CW,
        NULL);
    }
    /* Encryption not supported */
    if (!(block1->block2.CW&0x0500)) {
        parse_payload(data, block1, out);
    } else {
        data = data_append(data,
        "payload_encrypted", "Payload Encrypted", DATA_FORMAT, "1", DATA_INT, NULL,
                        NULL);
    }
    decoder_output_data(decoder, data);
}


static int m_bus_mode_c_t_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    static const uint8_t PREAMBLE_T[]  = {0x54, 0x3D};      // Mode T Preamble (always format A - 3of6 encoded)
//  static const uint8_t PREAMBLE_CA[] = {0x55, 0x54, 0x3D, 0x54, 0xCD};  // Mode C, format A Preamble
//  static const uint8_t PREAMBLE_CB[] = {0x55, 0x54, 0x3D, 0x54, 0x3D};  // Mode C, format B Preamble

    m_bus_data_t    data_in     = {0};  // Data from Physical layer decoded to bytes
    m_bus_data_t    data_out    = {0};  // Data from Data Link layer
    m_bus_block1_t  block1      = {0};  // Block1 fields from Data Link layer
    char *mode = "";

    // Validate package length
    if (bitbuffer->bits_per_row[0] < (32+13*8) || bitbuffer->bits_per_row[0] > (64+256*8)) {  // Min/Max (Preamble + payload)
        return DECODE_ABORT_LENGTH;
    }

    // Find a Mode T or C data package
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, PREAMBLE_T, sizeof(PREAMBLE_T)*8);
    if (bit_offset + 13*8 >= bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        return DECODE_ABORT_EARLY;
    }
    if (decoder->verbose) { fprintf(stderr, "PREAMBLE_T: found at: %d\n", bit_offset);
    bitbuffer_print(bitbuffer);
    }
    bit_offset += sizeof(PREAMBLE_T)*8;     // skip preamble

    uint8_t next_byte = bitrow_get_byte(bitbuffer->bb[0], bit_offset);
    bit_offset += 8;
    // Mode C
    if (next_byte == 0x54) {
        mode = "C";
        next_byte = bitrow_get_byte(bitbuffer->bb[0], bit_offset);
        bit_offset += 8;
        // Format A
        if (next_byte == 0xCD) {
            if (decoder->verbose) { fprintf(stderr, "M-Bus: Mode C, Format A\n"); }
            // Extract data
            data_in.length = (bitbuffer->bits_per_row[0]-bit_offset)/8;
            bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, data_in.data, data_in.length*8);
            // Decode
            if (!m_bus_decode_format_a(decoder, &data_in, &data_out, &block1))    return 0;
        } // Format A
        // Format B
        else if (next_byte == 0x3D) {
            if (decoder->verbose) { fprintf(stderr, "M-Bus: Mode C, Format B\n"); }
            // Extract data
            data_in.length = (bitbuffer->bits_per_row[0]-bit_offset)/8;
            bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, data_in.data, data_in.length*8);
            // Decode
            if (!m_bus_decode_format_b(decoder, &data_in, &data_out, &block1))    return 0;
        }   // Format B
        // Unknown Format
        else {
            if (decoder->verbose) {
                fprintf(stderr, "M-Bus: Mode C, Unknown format: 0x%X\n", next_byte);
                bitbuffer_print(bitbuffer);
            }
            return 0;
        }
    }   // Mode C
    // Mode T
    else {
        mode = "T";
        bit_offset -= 8; // Rewind offset to start of telegram
        if (decoder->verbose) { fprintf(stderr, "M-Bus: Mode T\n"); }
        if (decoder->verbose) { fprintf(stderr, "Experimental - Not tested\n"); }
        // Extract data
        data_in.length = (bitbuffer->bits_per_row[0]-bit_offset)/12;    // Each byte is encoded into 12 bits
        if (decoder->verbose) { fprintf(stderr, "MBus telegram length: %d\n", data_in.length); }
        if (m_bus_decode_3of6_buffer(bitbuffer->bb[0], bit_offset, data_in.data, data_in.length) < 0) {
            if (decoder->verbose) fprintf(stderr, "M-Bus: Decoding error\n");
            return 0;
        }
        // Decode
        if (!m_bus_decode_format_a(decoder, &data_in, &data_out, &block1))    return 0;
    }   // Mode T

    m_bus_output_data(decoder, &data_out, &block1, mode);
    return 1;
}


static int m_bus_mode_r_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
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

    if (decoder->verbose) { fprintf(stderr, "M-Bus: Mode R, Format A\n"); }
    if (decoder->verbose) { fprintf(stderr, "Experimental - Not tested\n"); }
    // Extract data
    data_in.length = (bitbuffer->bits_per_row[0]-bit_offset)/8;
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, data_in.data, data_in.length*8);
    // Decode
    if (!m_bus_decode_format_a(decoder, &data_in, &data_out, &block1))    return 0;

    m_bus_output_data(decoder, &data_out, &block1, "R");
    return 1;
}

// Untested code, signal samples missing
static int m_bus_mode_f_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
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
        if (decoder->verbose) { fprintf(stderr, "M-Bus: Mode F, Format A\n"); }
        if (decoder->verbose) { fprintf(stderr, "Not implemented\n"); }
        return 1;
    } // Format A
    // Format B
    else if (next_byte == 0x72) {
        if (decoder->verbose) { fprintf(stderr, "M-Bus: Mode F, Format B\n"); }
        if (decoder->verbose) { fprintf(stderr, "Not implemented\n"); }
        return 1;
    }   // Format B
    // Unknown Format
    else {
        if (decoder->verbose) {
            fprintf(stderr, "M-Bus: Mode F, Unknown format: 0x%X\n", next_byte);
            bitbuffer_print(bitbuffer);
        }
        return 0;
    }

    //m_bus_output_data(decoder, &data_out, &block1, "F");
    return 1;
}

static int m_bus_mode_s_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    static const uint8_t PREAMBLE_S[]  = {0x54, 0x76, 0x96};  // Mode S Preamble
    unsigned int start_pos;
    bitbuffer_t packet_bits = {0};
    m_bus_data_t    data_in     = {0};  // Data from Physical layer decoded to bytes
    m_bus_data_t    data_out    = {0};  // Data from Data Link layer
    m_bus_block1_t  block1      = {0};  // Block1 fields from Data Link layer

    // Validate package length
    if (bitbuffer->bits_per_row[0] < (32+13*8) || bitbuffer->bits_per_row[0] > (64+256*8)) {
        return DECODE_ABORT_LENGTH;
    }

    // Find a Mode S data package
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, PREAMBLE_S, sizeof(PREAMBLE_S)*8);
    start_pos = bitbuffer_manchester_decode(bitbuffer, 0, bit_offset+sizeof(PREAMBLE_S)*8, &packet_bits, 410);
    data_in.length = (bitbuffer->bits_per_row[0]);
    bitbuffer_extract_bytes(&packet_bits, 0, 0, data_in.data, data_in.length);

    if (!m_bus_decode_format_a(decoder, &data_in, &data_out, &block1))    return 0;

    m_bus_output_data(decoder, &data_out, &block1, "S");

    return 1;
}

static char *output_fields[] = {
    "model",
    "mode",
    "id",
    "version",
    "type",
    "type_string",
    "CI",
    "AC",
    "ST",
    "CW",
    "sn",
    "knx_ctrl",
    "src",
    "dst",
    "l_npci",
    "tpci",
    "apci",
    "crc",
    NULL
};

// Mode C1, C2 (Meter TX), T1, T2 (Meter TX),
// Frequency 868.95 MHz, Bitrate 100 kbps, Modulation NRZ FSK
r_device m_bus_mode_c_t = {
    .name           = "Wireless M-Bus, Mode C&T, 100kbps (-f 868950000 -s 1200000)",     // Minimum samplerate = 1.2 MHz (12 samples of 100kb/s)
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 10,   // Bit rate: 100 kb/s
    .long_width     = 10,   // NRZ encoding (bit width = pulse width)
    .reset_limit    = 500,  //
    .decode_fn      = &m_bus_mode_c_t_callback,
    .disabled       = 0,
    .fields         = output_fields,
};


// Mode S1, S1-m, S2, T2 (Meter RX),    (Meter RX not so interesting)
// Frequency 868.3 MHz, Bitrate 32.768 kbps, Modulation Manchester FSK
r_device m_bus_mode_s = {
    .name           = "Wireless M-Bus, Mode S, 32.768kbps (-f 868300000 -s 1000000)",   // Minimum samplerate = 1 MHz (15 samples of 32kb/s manchester coded)
    .modulation     = FSK_PULSE_PCM,
    .short_width    = (1000.0/32.768),   // ~31 us per bit
    .long_width     = (1000.0/32.768),
    .reset_limit    = ((1000.0/32.768)*9), // 9 bit periods
    .decode_fn      = &m_bus_mode_s_callback,
    .disabled       = 0,
    .fields         = output_fields,
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
    .short_width    = (1000.0f / 4.8f / 2),   // ~208 us per bit -> clock half period ~104 us
    .long_width     = 0,    // Unused
    .reset_limit    = (1000.0f / 4.8f * 1.5f), // 3 clock half periods
    .decode_fn      = &m_bus_mode_r_callback,
    .disabled       = 1,    // Disable per default, as it runs on non-standard frequency
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
    .short_width    = 1000.0f / 2.4f,   // ~417 us
    .long_width     = 1000.0f / 2.4f,   // NRZ encoding (bit width = pulse width)
    .reset_limit    = 5000,         // ??
    .decode_fn      = &m_bus_mode_f_callback,
    .disabled       = 1,    // Disable per default, as it runs on non-standard frequency
};
