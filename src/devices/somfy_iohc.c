/** @file
    Somfy io-homecontrol devices.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Somfy io-homecontrol devices.

E.g. Velux remote controller KI 313.

    rtl_433 -c 0 -R 0 -g 40 -X "n=uart,m=FSK_PCM,s=26,l=26,r=300,preamble={24}0x5555ff,decode_uart" -f 868.89M

Protocol description:

- Preamble is 55..55.
- The message, including the sync word is UART encoded, 8 data bits equal 10 packet bits.
- 16 bit sync word of ff33, UART encoded: 0 ff 1 0 cc 1 = 7fd99.
- 4+4 bit message type/length indicator byte.
- 32 bit destination address (little endian presumably).
- 32 bit source address (little endian presumably).
- n bytes variable length message payload bytes
- 16 bit MAC counter value
- 48 bit MAC value
- 16 bit CRC-16, poly 0x1021, init 0x0000, reflected.

Example packets:

    ff33 f6 2000003f dacdea00 016100000000     0bdd fd8ef56f15ad aa1e
    ff33 f6 0000003f dacdea00 016100000000     0bdd fd8ef56f15ad 4f9c
    ff33 f6 2000003f dacdea00 0161c8000000     0bbd 8aa3a9732e10 26d2
    ff33 f6 0000003f dacdea00 0161c8000000     0bbd 8aa3a9732e10 c350
    ff33 f6 2000003f dacdea00 0161d2000000     0b99 15decacf7e0e 8069
    ff33 f6 0000003f dacdea00 0161d2000000     0b99 15decacf7e0e 65eb
    ff33 f6 0000003f dacdea00 0161d2000000     0ba1 05175a82dfae 8bbf

    ff33 f8 0000007f e1f57300 0161d40080c80000 0d6c 2c3a3123e6ab 7f1e [UP RIGHT]
    ff33 f8 0000007f e1f57300 0161d40080c80000 0d6e e448de7d4e03 62d1 [UP RIGHT]
    ff33 f8 0000007f c5896700 0161d40080c80000 0c63 04e867ed64ad f055 [UP LEFT]
    ff33 f8 0000007f c5896700 0161d40080c80000 0c65 8414991e8b06 b82b [UP LEFT]
    ff33 f8 0000007f 70875800 0161d40080c80000 3bd5 05526875499c 7e72 [UP PSA]
    ff33 f6 0000003f e1f57300 0161d2000000     0d6f 708d89781e43 bc24 [STOP RIGHT]
    ff33 f6 0000003f e1f57300 0161d2000000     0d71 d1b10f26e1c1 8a9d [STOP RIGHT]
    ff33 f6 0000003f c5896700 0161d2000000     0c66 4fcf56fb1c72 d31e [STOP LEFT]
    ff33 f6 0000003f c5896700 0161d2000000     0c68 2025e049f331 b64a [STOP LEFT]
    ff33 f6 0000003f 70875800 0161d2000000     3bd2 e6b62cef54c8 a937 [STOP PSA]
    ff33 f6 0000003f 70875800 0161d2000000     3bd6 630743f0530d dc24 [STOP PSA]
    ff33 f8 0000007f e1f57300 0161d40080c80000 0d74 9fb9a0665ff4 77a6 [DOWN RIGHT]
    ff33 f8 0000007f e1f57300 0161d40080c80000 0d76 71b81065a2e2 0616 [DOWN RIGHT]
    ff33 f8 0000007f c5896700 0161d40080c80000 0c6b 56fcf691e6a9 2c74 [DOWN LEFT]
    ff33 f8 0000007f c5896700 0161d40080c80000 0c6d daf020864668 8fad [DOWN LEFT]
    ff33 f8 0000007f 70875800 0161d40080c80000 3bdf 1ee7a0e30448 7a6b [DOWN PSA]

    ff33 f6 0000003f 17f52300 0147c8000000     18c4 38789cb680cc bc74
    ff33 f8 0000003f 17f52320 02ff0143010e0000 18c5 045ee107363d 59b4
    ff33 f8 0000003f 17f52320 02ff01430105ff00 18c6 a34715cbe012 4f7f
    ^    ^  ^        ^        ^                ^    ^            ^CRC
    ^    ^  ^        ^        ^                ^    ^MAC
    ^    ^  ^        ^        ^                ^counter
    ^    ^  ^        ^        ^payload
    ^    ^  ^        ^source
    ^    ^  ^destination
    ^    ^length of payload
    ^sync, not included in CRC

    ff33 f8 0000007f e1f57300 0161d40080c80000 0d9a 4c0e4a0e45aa 995d
    ff33 f8 0000007f c5896700 0161d40080c80000 0d9a 87ac970b53d1 d80d
    ff33 f8 0000007f e1f57300 0161d40080c80000 0d9c ec44bf478a06 0cad
    ff33 f8 0000007f c5896700 0161d40080c80000 0d9c f610e5e0eb9f 6ba1
    ff33 f8 0000007f c5896700 0161d40080c80000 0d9e 10707b9a1f4a ed84
    ff33 f8 0000007f e1f57300 0161d40080c80000 0d9e de2cc3c1117a f8fd
    ff33 f8 0000007f c5896700 0161d40080c80000 0da0 11280dad6fe7 b06a
    ff33 f8 0000007f e1f57300 0161d40080c80000 0da0 209550b7e353 0a35
    ff33 f8 0000007f c5896700 0161d40080c80000 0da2 445f4c017d24 0cde
    ff33 f8 0000007f e1f57300 0161d40080c80000 0da2 877cc2f46a38 8a5c
    ff33 f8 0000007f e1f57300 0161d40080c80000 0da4 4412916d3fe6 42b8
    ff33 f8 0000007f c5896700 0161d40080c80000 0da4 787e20402f19 6b43
    ff33 f8 0000007f e1f57300 0161d40080c80000 0da6 0eb34055b18f 1209
    ff33 f8 0000007f c5896700 0161d40080c80000 0da6 33a9c2c0cc66 5e5d
    ff33 f8 0000007f e1f57300 0161d40080c80000 0da8 07c9a103761b 1f5b
    ff33 f8 0000007f c5896700 0161d40080c80000 0da8 b3f7dd7a366f de5e
    ff33 f8 0000007f c5896700 0161d40080c80000 0daa 80075cf2e7bc 6064
    ff33 f8 0000007f e1f57300 0161d40080c80000 0daa eb6adf4f8fad 3404

*/

static int somfy_iohc_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x57, 0xfd, 0x99};

    uint8_t b[1 + 31 + 2]; // Length, payload, CRC

    if (bitbuffer->num_rows != 1)
        return DECODE_ABORT_EARLY;

    unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 24) + 24;
    if (offset >= bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;
    int num_bits = bitbuffer->bits_per_row[0] - offset;

    num_bits = MIN((size_t)num_bits, sizeof (b) * 8);

    int len = extract_bytes_uart(bitbuffer->bb[0], offset, num_bits, b);
    if (len < 11)
        return DECODE_ABORT_LENGTH;

    // Mandatory fields
    // Control byte 1
    // end_flag : 1
    // start_flag : 1
    // protocol_mode : 1
    // frame_length : 5
    int msg_len = b[0] & 0x1f;
    if (len < msg_len + 3)
        return DECODE_ABORT_LENGTH;
    if (msg_len < 8)
        return DECODE_ABORT_LENGTH;
    len = msg_len + 3;

    int msg_end_flag      = (b[0] & 0x80) >> 7;
    int msg_start_flag    = (b[0] & 0x40) >> 6;
    int msg_protocol_mode = (b[0] & 0x20) >> 5;

    // Control byte 2
    // use_beacon : 1
    // is_routed : 1
    // low_power_mode : 1
    // protocol_version : 3
    int msg_use_beacon       = (b[1] & 0x80) >> 7;
    int msg_is_routed        = (b[1] & 0x40) >> 6;
    int msg_low_power_mode   = (b[1] & 0x20) >> 5;
    int msg_protocol_version = b[1] & 0x03;

    // Addresses
    // dst_addr : 24
    // src_addr : 24
    int msg_dst_addr = (b[2] << 16) | (b[3] << 8) | b[4];
    int msg_src_addr = (b[5] << 16) | (b[6] << 8) | b[7];

    // Command ID
    // cmd_id : 8
    int msg_cmd_id = b[8];

    // optional fields
    int msg_seq_nr = 0;
    char msg_mac[13] = {0};

    char msg_data[31 * 2 + 1]; // variable length, converted to hex string
    unsigned int data_length = msg_len - 8;
    if (msg_protocol_mode == 0 || data_length < 8) {
        bitrow_snprint(&b[9], data_length * 8, msg_data, sizeof (msg_data));
    } else {
        data_length -= 8;
        bitrow_snprint(&b[9], data_length * 8, msg_data, sizeof (msg_data));
        msg_seq_nr = (b[9 + data_length] << 8) | b[9 + data_length + 1];
        bitrow_snprint(&b[9 + data_length + 2], 6 * 8, msg_mac, sizeof msg_mac);
    }

    // crc : 16;
    //int msg_crc = (b[len - 2] << 8) | b[len - 1];

    // calculate and verify checksum
    if (crc16lsb(b, len, 0x8408, 0x0000) != 0) // unreflected poly 0x1021
        return DECODE_FAIL_MIC;

    decoder_logf_bitrow(decoder, 2, __func__, b, len * 8, "offset %u, num_bits %u, len %d, msg_len %d", offset, num_bits, len, msg_len);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Somfy-IOHC",
            "id",               "Source",           DATA_FORMAT, "%06x", DATA_INT, msg_src_addr,
            "dst_id",           "Target",           DATA_FORMAT, "%06x", DATA_INT, msg_dst_addr,
            "msg_type",         "Command",          DATA_FORMAT, "%02x", DATA_INT, msg_cmd_id,
            "msg",              "Message",          DATA_STRING, msg_data,
            "mode",             "Mode",             DATA_STRING, msg_protocol_mode ? "One-way" : "Two-way",
            "version",          "Version",          DATA_INT,    msg_protocol_version,
            "counter",          "Counter",          DATA_COND, msg_protocol_mode == 1, DATA_INT,    msg_seq_nr,
            "mac",              "MAC",              DATA_COND, msg_protocol_mode == 1, DATA_STRING, msg_mac,
            "flag_end",         "End flag",         DATA_INT,    msg_end_flag,
            "flag_start",       "Start flag",       DATA_INT,    msg_start_flag,
            "flag_mode",        "Mode flag",        DATA_INT,    msg_protocol_mode,
            "flag_beacon",      "Beacon flag",      DATA_INT,    msg_use_beacon,
            "flag_routed",      "Routed flag",      DATA_INT,    msg_is_routed,
            "flag_lpm",         "LPM flag",         DATA_INT,    msg_low_power_mode,
            "mic",              "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "dst_id",
        "msg_type",
        "msg",
        "mode",
        "version",
        "counter",
        "mac",
        "flag_end",
        "flag_start",
        "flag_mode",
        "flag_beacon",
        "flag_routed",
        "flag_lpm",
        "mic",
        NULL,
};

// rtl_433 -c 0 -R 0 -g 40 -X "n=uart,m=FSK_PCM,s=26,l=26,r=300,preamble={24}0x57fd99,decode_uart" -f 868.89M
r_device const somfy_iohc = {
        .name        = "Somfy io-homecontrol",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 26,
        .long_width  = 26,
        .reset_limit = 300, // UART encoding has at most 9 0's, nominal 234 us.
        .decode_fn   = &somfy_iohc_decode,
        .fields      = output_fields,
};
