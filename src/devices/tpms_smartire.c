/** @file
    SmarTire TPMS sensor.

    Copyright (C) 2024 Bruno OCTAU (ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
SmarTire TPMS sensor.
- SmarTire Vantage / Aston Martin DB9 protocol, from 1/2005 till 12/2011

S.a. issue #3067

Data Layout:
- Total of 10 messages at a time, OOK PCM and Differential MC coded.
- 2 types of message have been identified.
- 1 Message with Pressure information follow by
- 1 Message with Temperature information
- Both messages are repeated 5 times
- In case of fast pressure increasing, the pressure message is sent with Fast increased flag, then the 10 messages each 2 seconds with the flag.

Flex decoder:

    rtl_433 -X 'n=SmarTire-AM,m=OOK_PCM,s=167,l=167,r=600,preamble=32b4,decode_dm'

Preamble/Syncword  .... : 0x32b4

    6 bytes message
    Byte Position   0  1  2  3  4  5
    Sample         28 3f ff ff 00 fa
                   VV |I II II || CC
                      |        ||
                 +----+-+  +----------+
                 | 1234 |  | 12345678 |
                 | MMII |  | FFFFFFXX |
                 +------+  +----------+

- VV: {8} Pressure value, offset 40, scale 2.5 if Message Type = 0x00
          Temperature Value, offset 40,        if Message Type = 0x01
- MM: {2} Message Type, 0b00 or 0b01
- II:{22} Sensor ID,
- F : {8} Flags, F1 = quick inflate detected,
                 F2 to F6 are unknown,
                 X78:{2} Looks like XOR/checksum/Parity from previous bits, not decoded.
- C : {7} CRC-7, poly 0x45, init 0x6f, final XOR 0x00

Bitbench:

    TEMP/PRESSURE 8d MESSAGE_TYPE 2b ID 22d FAST_INCREASE 1b FLAGS ? 5b PARITY_XOR ? 2b CRC_SEVEN 7b 1x

*/

static int tpms_smartire_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_t decoded = { 0 };
    uint8_t *b;
    uint8_t const preamble_pattern[] = {0x32, 0xb4};
    uint8_t len_msg = 6;
    float pressure_kPa = 0;
    int temperature_C = 0;

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);
    if (pos >= bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 1, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    decoder_log_bitrow(decoder, 1, __func__, bitbuffer->bb[0], bitbuffer->bits_per_row[0], "MSG");
    bitbuffer_differential_manchester_decode(bitbuffer, 0, pos + sizeof(preamble_pattern) * 8, &decoded, len_msg * 8);
    decoder_log_bitrow(decoder, 1, __func__, decoded.bb[0], decoded.bits_per_row[0], "DMC");

    // check msg length
    if (decoded.bits_per_row[0] < (len_msg * 8) - 1 ) { // always missing last bit
        decoder_logf(decoder, 1, __func__, "Too short");
        return DECODE_ABORT_LENGTH;
    }

    b = decoded.bb[0];

    // verify checksum
    if (crc7(b, len_msg, 0x45, 0x6f)) {
        decoder_logf(decoder, 1, __func__, "crc error");
        return DECODE_FAIL_MIC; // crc mismatch
    }

    int id       = ((b[1] & 0x3f) << 16) | (b [2] << 8) | b[3];
    int msg_type = (b[1] & 0xc0) >> 6;
    int value    = b[0] - 40;

    if (msg_type == 0) { // pressure
        pressure_kPa = value * 2.5;
    }
    else if (msg_type == 1) { // temperature
        temperature_C = value;
    }
    else {
        decoder_logf(decoder, 1, __func__, "Unknown message type %x", msg_type);
        return DECODE_ABORT_EARLY;
    }

    int inflate = (b[4] & 0x80) >> 7;
    int flags   = b[4] & 0x7f;

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",              DATA_STRING, "SmarTire-AM", // Aston Martin model
            "type",          "",              DATA_STRING, "TPMS",
            "id",            "",              DATA_INT,    id,
            "pressure_kPa",  "Pressure",      DATA_COND, msg_type == 0, DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "temperature_C", "Temperature",   DATA_COND, msg_type == 1, DATA_FORMAT, "%.1f C",   DATA_DOUBLE, (double)temperature_C,
            "inflate",       "Inflate",       DATA_COND, inflate == 1,  DATA_INT, 1,
            "flags",         "Flags",         DATA_FORMAT, "%07b", DATA_INT, flags,
            "mic",           "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_kPa",
        "temperature_C",
        "inflate",
        "flags",
        "mic",
        NULL,
};

r_device const tpms_smartire = {
        .name        = "SmarTire TPMS sensor, Aston Martin/Vantage DB9 protocol",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 167,
        .long_width  = 167,
        .reset_limit = 1000,
        .decode_fn   = &tpms_smartire_decode,
        .fields      = output_fields,
};
