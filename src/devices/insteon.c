/** @file
    Insteon RF decoder.

    Copyright (C) 2020 Peter Shipley

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/** @fn int parse_insteon_pkt(r_device *decoder, bitbuffer_t *bits, unsigned int row, unsigned int start_pos)
Insteon RF decoder.

    "Insteon is a home automation (domotics) technology that enables
    light switches, lights, thermostats, leak sensors, remote controls,
    motion sensors, and other electrically powered devices to interoperate
    through power lines, radio frequency (RF) communications, or both
    [ from wikipedia ]

the Insteon RF protocol is a series of 28 bit packets containing one byte of data


Each byte (X) is encoded as 28 bits:
>     '11' followed by
>     5 bit index number (manchester encoded)
>     8 bit byte (manchester encoded)

All values are written in LSB format (Least Significant Bit first)

The first byte is always transmitted with a index of 32 (11111)
all following bytes are transmitted with a decrementing index count with the final byte with index 0

    Dat   index dat         LSB index dat     manchester                     '11' + manchester
    03 -> 11111 00000011 -> 11111 11000000 -> 0101010101 0101101010101010 -> 1101010101010101101010101010
    E5 -> 01011 11100101 -> 11010 10100111 -> 0101100110 0110011010010101 -> 1101011001100110011010010101
    3F -> 01010 00111111 -> 01010 11111100 -> 1001100110 0101010101011010 -> 1110011001100101010101011010
    16 -> 01001 00010110 -> 01010 11111100 -> 0110100110 1001011001101010 -> 1101101001101001011001101010

[Insteon RF Toolkit](https://github.com/evilpete/insteonrf/Doc)

## Printed packet format notation

   *flag* **:** *to_address* **:** *from_address* : command_data crc

`43 : 226B3F : 2B7811 : 13 01  35`

## Settings

- Frequency: 915MHz
- SampleRate: 1024K
- Modulation: FSK

*/

#include "decoder.h"

// 1100111010101010
static uint8_t const insteon_preamble[] = {0xCE, 0xAA};

#define INSTEON_PACKET_MIN 10
#define INSTEON_PACKET_MAX 13
#define INSTEON_PACKET_MIN_EXT 23
#define INSTEON_PACKET_MAX_EXT 32
#define INSTEON_BITLEN_MIN (INSTEON_PACKET_MIN * 28) + sizeof(insteon_preamble)
#define INSTEON_PREAMBLE_LEN 16


/*
    calc checksum of extended packet data
    (differs from normal packet)

    takes an instion packet in form of a list of uint8_t
    and returns CRC in the form of a uint8_t

    using :
        ((Not(sum of cmd1..d13)) + 1) and 255
*/

static uint8_t gen_ext_crc(uint8_t *dat)
{
    uint8_t r = 0;

    for (int i = 7; i < 22; i++) {
        r += dat[i];
    }

    r = ~r;
    r = r + 1;
    r = (r & 0xFF);

    return ((uint8_t)r);
}

/*
    calc checksum of normal packet data
    (differs from extended packet)

    takes an instion packet in form of a list of uint8_t
    and returns uint8_t the CRC for RF packet
*/
static uint8_t gen_crc(uint8_t *dat)
{
    uint8_t r = 0;

    for (int i = 0; i < 9; i++) {
        r ^= dat[i];
        r ^= ((r ^ (r << 1)) & 0x0F) << 4;
    }

    return (r);
}

static int parse_insteon_pkt(r_device *decoder, bitbuffer_t *bits, unsigned int row, unsigned int start_pos)
{
    uint8_t results[35]   = {0};
    uint8_t results_len   = 0;
    bitbuffer_t i_bits    = {0};
    bitbuffer_t d_bits    = {0};
    unsigned int next_pos = 0;
    uint8_t i             = 0;
    uint8_t pkt_i, pkt_d;

    // move past preamble
    start_pos += 7;

    /*
    We are looking as something line this
        110101010101010110101010101011....

    which we an break down as

        11 0101010101 0101101010101010 11....

        "11" + 10 manchester bits LSB + 16 manchester bits LSB + "11"

        we decode this into a
            5 bits LSB (always 32 in the first block)
            8 bits LSB (flag bits for the upcoming packet


        Flag fields (MSB format):
            "maxhops"  = (flag & 0b00000011)
            "hopsleft" = (flag & 0b00001100)
            "extended" = (flag & 0b00010000)
            "ack"      = (flag & 0b00100000)
            "group"    = (flag & 0b01000000)
            "bcast"    = (flag & 0b10000000)
            "mtype"    = (flag & 0b11100000)

        (we can discard the 5 bit digit)

        after this we can index forward 28 bits (2 + 10 + 16)

    */

    next_pos = bitbuffer_manchester_decode(bits, row, start_pos, &i_bits, 5);
    pkt_i    = reverse8(i_bits.bb[0][0]);

    next_pos               = bitbuffer_manchester_decode(bits, row, next_pos, &d_bits, 8);
    pkt_d                  = reverse8(d_bits.bb[0][0]);
    results[results_len++] = pkt_d;

    if (pkt_i != 31) { // should always be 31 (0b11111) in first block of packet
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_extract_bytes(bits, row, start_pos + 26, &i, 2);
    // Check for packet delimiter  marker bits (at least once)
    if (i != 0xc0) {                 // 0b11000000
        return DECODE_FAIL_SANITY; // There should be two high bits '11' between packets
    }

    // printBits(sizeof(d), &d);
    int extended        = 0;

    uint8_t max_pkt_len = INSTEON_PACKET_MAX;
    uint8_t min_pkt_len = INSTEON_PACKET_MIN;
    if (results[0] & 0x10) {
        extended    = 1;
        max_pkt_len = INSTEON_PACKET_MAX_EXT;
        min_pkt_len = INSTEON_PACKET_MIN_EXT;
    }

    decoder_logf(decoder, 1, __func__, "start_pos %u row_length %hu =  %u",
            start_pos, bits->bits_per_row[row], (bits->bits_per_row[row] - start_pos));

    {
    decoder_log(decoder, 1, __func__, "pkt_i pkt_d next length count");
    uint8_t buffy[4];
    bitbuffer_extract_bytes(bits, row, start_pos - 2, buffy, 30);
    decoder_logf_bitrow(decoder, 1, __func__, buffy, 30, "%2d %02X %03u %u %2d",
            pkt_i, pkt_d, next_pos, (next_pos - start_pos), 0);
    }

    /*   Is this overkill ??
    unsigned int l;
    if (extended) {
         l = 642;
     } else {
         l = 278;
     }
     if ((bits->bits_per_row[row] - start_pos)  < l) {
        decoder_logf(decoder, 1, __func__, "row to short for %s packet type",
                (extended ? "extended" : "regular"));
        return DECODE_ABORT_LENGTH;     // row to short for packet type
     }
     */

    /*
        The data is contained in 26bit blocks containing 26bit manchester
        the resulting 13bits contains 5bit of packet index
        and 8bits of data
    */
    uint8_t prev_i=33;
    for (int j = 1; j < max_pkt_len; j++) {
        unsigned y;
        start_pos += 28;
        bitbuffer_clear(&i_bits);
        bitbuffer_clear(&d_bits);
        next_pos = bitbuffer_manchester_decode(bits, row, start_pos, &i_bits, 5);
        next_pos = bitbuffer_manchester_decode(bits, row, next_pos, &d_bits, 8);

        y = (next_pos - start_pos);
        if (y != 26) {
            decoder_logf(decoder, 1, __func__, "stop %u != 26", y);
            break;
        }

        // bitbuffer_extract_bytes(bits, row, start_pos -2, buff, 8);
        // printBits(sizeof(buff), buff);

        pkt_i = reverse8(i_bits.bb[0][0]);
        pkt_d = reverse8(d_bits.bb[0][0]);

        results[results_len++] = pkt_d;

        {
        uint8_t buffy[4];
        bitbuffer_extract_bytes(bits, row, start_pos - 2, buffy, 30);
        decoder_logf_bitrow(decoder, 1, __func__, buffy, 30, "%2d %02X %03u %u %2d",
                pkt_i, pkt_d, next_pos, (next_pos - start_pos), j);
        // parse_insteon_pkt: curr packet (3f) { 1} d6 : 1
        }

        // packet index should decrement
        if (pkt_i < prev_i) {
            prev_i = pkt_i;
        } else {
            return DECODE_ABORT_EARLY;
        }
    }

    // decoder_log_bitrow(decoder, 2, __func__, results, results_len * 8, "results");

    if (results_len < min_pkt_len) {
        decoder_logf(decoder, 2, __func__, "fail: short packet %d < 9", results_len);
        return 0;
    }

    uint8_t crc_val;
    if (extended) {
        crc_val = gen_ext_crc(results);
    }
    else {
        crc_val = gen_crc(results);
    }

    if (results[min_pkt_len - 1] != crc_val) {
        decoder_logf(decoder, 2, __func__, "fail: bad CRC %02X != %02X %s", results[min_pkt_len], crc_val,
                    (extended ? "extended" : ""));
        return DECODE_FAIL_MIC;
    }

    char pkt_from_addr[8]   = {0};
    char pkt_to_addr[32]    = {0};
    char pkt_formatted[256] = {0};
    char cmd_str[92]        = {0};

    snprintf(pkt_to_addr, sizeof(pkt_to_addr), "%02X%02X%02X",
            results[3], results[2], results[1]);
    snprintf(pkt_from_addr, sizeof(pkt_from_addr), "%02X%02X%02X",
            results[6], results[5], results[4]);

    char *p = cmd_str;
    int cmd_array[32];
    int cmd_array_len = 0;
    for (int j = 7; j < min_pkt_len - 1; j++) {
        p += sprintf(p, "%02X ", results[j]);
        cmd_array[cmd_array_len++] = (int)results[j];
    }

    char payload[INSTEON_PACKET_MAX_EXT * 2 + 2] = {0};
    p                = payload;
    for (int j = 0; j < results_len; j++) {
        p += sprintf(p, "%02X", results[j]);
    }

    snprintf(pkt_formatted, sizeof(pkt_formatted), "%02X : %s : %s : %s %02X",
            results[0], pkt_to_addr, pkt_from_addr, cmd_str, results[min_pkt_len - 1]);

    /*
    flag = b[0]
    "maxhops"  = (flag & 0b00000011)
    "hopsleft" = (flag & 0b00001100)
    "extended" = (flag & 0b00010000)
    "ack"      = (flag & 0b00100000)
    "group"    = (flag & 0b01000000)
    "bcast"    = (flag & 0b10000000)
    "mtype"    = (flag & 0b11100000)
    */

    int hopsmax = (results[0] & 0x03);
    int hopsleft = (results[0] >> 2) & 0x03;

    // char hops_str[8] = {0};
    // snprintf(hops_str, sizeof(hops_str), "%d / %d",
    //         (results[0] & 0x03),
    //         (results[0] >> 2) & 0x03);

    int pkt_type = (results[0] >> 5) & 0x07;
    char const *const messsage_text[8] = {
            "Direct Message",                         // 000
            "ACK of Direct Message",                  // 001
            "Group Cleanup Direct Message",           // 010
            "ACK of Group Cleanup Direct Message",    // 011
            "Broadcast Message",                      // 100
            "NAK of Direct Message",                  // 101
            "Group Broadcast Message",                // 110
            "NAK of Group Cleanup Direct Message"};   // 111

    char const *pkt_type_str = messsage_text[pkt_type];
    // decoder_log_bitrow(decoder, 0, __func__, results, 8, "Flag");
    //decoder_logf(decoder, 0, __func__, "pkt_type: %02X", pkt_type);

    decoder_logf_bitrow(decoder, 2, __func__, results, min_pkt_len * 8, "type %s", pkt_type_str);

    // Format data
    /*
    int data_payload[35];
    for (int j = 0; j < min_pkt_len; j++) {
        data_payload[j] = (int)results[j];
    }
    */

    /* clang-format off */
    data_t *data = data_make(
            "model",     "",                DATA_STRING, "Insteon",
         // "id",        "",                DATA_INT,    sensor_id,
         // "data",     "Data",             DATA_INT,    value,
            "from_id",   "From_Addr",       DATA_STRING, pkt_from_addr,
            "to_id",     "To_Addr",         DATA_STRING, pkt_to_addr,
            "msg_type",  "Message_Type",    DATA_INT,    pkt_type,
            "msg_str",   "Message_Str",     DATA_STRING, pkt_type_str,
         //   "command",   "Command",         DATA_STRING, cmd_str,
            "extended",  "Extended",        DATA_INT,    extended,
         // "hops",      "Hops",            DATA_STRING, hops_str,
            "hopsmax",   "Hops_Max",        DATA_INT,    hopsmax,
            "hopsleft",  "Hops_Left",       DATA_INT,    hopsleft,
            "formatted", "Packet",          DATA_STRING, pkt_formatted,
            "mic",       "Integrity",       DATA_STRING, "CRC",
            "payload",   "Payload",         DATA_STRING, payload,
            "cmd_dat",   "CMD_Data",        DATA_ARRAY,  data_array(cmd_array_len, DATA_INT, cmd_array),
        //  "payload",   "Payload",         DATA_ARRAY,  data_array(min_pkt_len, DATA_INT, data_payload),
            NULL);

    /* clang-format on */
    decoder_output_data(decoder, data);

    // Return 1 if message successfully decoded
    return 1;
}

/**
Insteon RF decoder.
@sa parse_insteon_pkt()
*/
static int insteon_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // unsigned int pkt_start_pos;
    uint16_t row;
    unsigned int ret_value = 0;
    int fail_value         = 0;
    // unsigned int pkt_cnt   = 0;

    // decoder_logf(decoder, 2, __func__, "row complete row / bit_index : %d, %d", row, bit_index);

    decoder_logf(decoder, 2, __func__, "new buffer %hu rows", bitbuffer->num_rows);

    bitbuffer_invert(bitbuffer);

    /*
     * loop over all rows and look for preamble
    */
    for (row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned bit_index = 0;
        // Validate message and reject it as fast as possible : check for preamble

        if (bitbuffer->bits_per_row[row] < INSTEON_BITLEN_MIN) {
            // decoder_logf(decoder, 1, __func__, "short row row=%hu len=%hu", row, bitbuffer->bits_per_row[row]);
            fail_value = DECODE_ABORT_LENGTH;
            continue;
        }
        // decoder_logf(decoder, 1, __func__, "New row=%d len=%d",  row, bitbuffer->bits_per_row[row]);

        while (1) {
            unsigned search_index = bit_index;
            int ret;

            if ((bitbuffer->bits_per_row[row] - bit_index) < INSTEON_BITLEN_MIN) {
                 // decoder_log(decoder, 2, __func__, "short remainder");
                 break;
             }

            decoder_logf(decoder, 2, __func__, "bitbuffer_search at row / search_index : %d, %u %u (%d)",
                        row, search_index, bit_index, bitbuffer->bits_per_row[row]);

            search_index = bitbuffer_search(bitbuffer, row, search_index, insteon_preamble, INSTEON_PREAMBLE_LEN);

            if (search_index >= bitbuffer->bits_per_row[row]) {
                if (bit_index == 0)
                    decoder_logf(decoder, 2, __func__, "insteon_preamble not found %u %u %d",
                        search_index, bit_index, bitbuffer->bits_per_row[row]);
                break;
            }

            decoder_logf(decoder, 1, __func__, "parse_insteon_pkt at: row / search_index : %hu, %u (%hu)",
                        row, search_index, bitbuffer->bits_per_row[row]);

            ret = parse_insteon_pkt(decoder, bitbuffer, row, search_index);

            // decoder_logf(decoder, 1, __func__, "parse_insteon_pkt ret value %d", ret_value);
            if (ret > 0) { // preamble good, decode good
                ret_value += ret;
                bit_index = search_index + INSTEON_BITLEN_MIN; // move a full packet length
            }
            else { // preamble good, decode fail
                if (ret < 0)
                    fail_value = ret;
                bit_index = search_index + INSTEON_PREAMBLE_LEN; // move to next preamble
            }
        }
    }

    if (ret_value > 0)
        return 1;
    else
        return fail_value;
}

/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */

static char const *const output_fields[] = {
        "model",
        // "id",
        // "data",
        "from_id",
        "to_id",
        "msg_type",     // packet type at int
        "msg_type_str",  // packet type as formatted string
        // "command",
        "extended",     // 0= short pkt, 1=extended pkt
        "hops_max",     // almost always 3
        "hops_left",    // remaining hops
        "formatted",   // entire packet as a formatted string with hex
        "mic",
        "payload",      // packet as a hex string
        "cmd_dat",      // array of int containing command + data
        "msg_str",
        "hopsmax",
        "hopsleft",
        // "raw",
        // "raw_message",
        NULL,
};

//     -X 'n=Insteon_F16,m=FSK_PCM,s=110,l=110,t=15,g=20000,r=20000,invert,match={16}0x6666'

r_device const insteon = {
        .name        = "Insteon",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 110, // short gap is 132 us
        .long_width  = 110, // long gap is 224 us
        .gap_limit   = 500, // some distance above long
        .tolerance   = 15,
        .reset_limit = 1000, // a bit longer than packet gap
        .decode_fn   = &insteon_callback,
        .fields      = output_fields,
};
