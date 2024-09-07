/** @file
    Decoder for Visonic Powercode devices. Tested with an MCT-302.

    Copyright (C) 2020 Maxwell Lock

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
The device uses OOK PWM encoding, short pulse 400us long pulse 800us, and repeats 6 times
You can use a flex decoder -X 'n=visonic_powercode,m=OOK_PWM,s=400,l=800,r=5000,g=900,t=160,y=0'

Powercode packet structure is 37 bits. 4 examples follow

              s addr                       data     cksm
              1 01101111 01000111 01110000 10001100 1001 - magnet near, case open
              1 01101111 01000111 01110000 11001100 1101 - magnet away, case open
              1 01101111 01000111 01110000 00001100 0001 - magnet near, case closed
              1 01101111 01000111 01110000 01001100 0101 - magnet away, case closed
              | |                        | |||||||| |  |
     StartBit_/ /                        / |||||||| \__\_checksum, XOR of preceding nibbles
     DeviceID__/________________________/  ||||||||
                                           ||||||||
                                    Tamper_/||||||\_Repeater
                                      Alarm_/||||\_Spidernet
                                     Battery_/||\_Supervise
                                         Else_/\_Restore

1 bit start bit
3 byte(24 bit) device ID
1 byte data
1 nibble (4 bit) checksum

Checksum is a londitudinal redundancy check of the 4 bytes containing the device ID and data.
Bytes are split into nibbles. 1st bit of each nibble is XORed and result is 1st bit of checksum,
then the same for the 2nd, 3rd and 4th bits.

Protocol cribbed from:
 * Visonic MCR-300 UART Manual http://www.el-sys.com.ua/wp-content/uploads/MCR-300_UART_DE3140U0.pdf
 * https://metacpan.org/release/Device-RFXCOM/source/lib/Device/RFXCOM/Decoder/Visonic.pm
 * https://forum.arduino.cc/index.php?topic=289554.0
*/

#include "decoder.h"

static int visonic_powercode_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t msg[32];
    uint8_t lrc;

    // 37 bits expected, 6 packet repetitions, accept 4
    int row = bitbuffer_find_repeated_row(bitbuffer, 4, 37);

    // exit if anything other than one row returned (-1 if failed)
    if (row != 0)
        return DECODE_ABORT_LENGTH;

    // exit if incorrect number of bits in row or none
    if (bitbuffer->bits_per_row[row] != 37)
        return DECODE_ABORT_LENGTH;

    // extract message, drop leading start bit, include trailing LRC nibble
    bitbuffer_extract_bytes(bitbuffer, row, 1, msg, 36);

    // No need to decode/extract values for simple test
    if (!msg[0] && !msg[1] && !msg[2] && !msg[3] && !msg[4]) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00");
        return DECODE_FAIL_SANITY;
    }

    lrc = xor_bytes(msg, 5);
    if (((lrc >> 4) ^ (lrc & 0xf)) != 0)
        return DECODE_FAIL_MIC;

    // debug
    decoder_logf(decoder, 2, __func__, "data byte is %02x", msg[3]);

    // format device id
    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x", msg[0], msg[1], msg[2]);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "Model",        DATA_STRING, "Visonic-Powercode",
            "id",           "ID",           DATA_STRING, id_str,
            "tamper",       "Tamper",       DATA_INT,    ((0x80 & msg[3]) == 0x80) ? 1 : 0,
            "alarm",        "Alarm",        DATA_INT,    ((0x40 & msg[3]) == 0x40) ? 1 : 0,
            "battery_ok",   "Battery",      DATA_INT,    ((0x20 & msg[3]) == 0x20) ? 0 : 1,
            "else",         "Else",         DATA_INT,    ((0x10 & msg[3]) == 0x10) ? 1 : 0,
            "restore",      "Restore",      DATA_INT,    ((0x08 & msg[3]) == 0x08) ? 1 : 0,
            "supervised",   "Supervised",   DATA_INT,    ((0x04 & msg[3]) == 0x04) ? 1 : 0,
            "spidernet",    "Spidernet",    DATA_INT,    ((0x02 & msg[3]) == 0x02) ? 1 : 0,
            "repeater",     "Repeater",     DATA_INT,    ((0x01 & msg[3]) == 0x01) ? 1 : 0,
            "mic",          "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    // return data
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "tamper",
        "alarm",
        "battery_ok",
        "else",
        "restore",
        "supervised",
        "spidernet",
        "repeater",
        "mic",
        NULL,
};

r_device const visonic_powercode = {
        .name        = "Visonic powercode",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 400,
        .long_width  = 800,
        .gap_limit   = 900,
        .reset_limit = 5000,
        .decode_fn   = &visonic_powercode_decode,
        .fields      = output_fields,
};
