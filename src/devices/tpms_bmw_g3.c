/** @file
    BMW Gen3 TPMS sensor.

    Copyright (C) 2024 Bruno OCTAU (ProfBoc75), \@Billymazze

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
BMW Gen3 TPMS sensor.

issue #2893 BMW Gen3 TPMS support open by \@Billymazze

Last progress based on this:

    rtl_433 -Y autolevel -Y minmax -X "n=BMW_G3,m=FSK_PCM,s=52,l=52,r=1000,preamble=cccd,decode_dm,bits>=190" *.cs8 2>&1 | grep "\{89\}"
    codes : {89}1c50f1758545f8020373428

RF signal:

    FSK, PCM, s=l=52 µs, Differential Manchester

Data layout{89} 11 x 8:

    Byte Position  0  1  2  3  4  5  6  7  8  9 10 11
    Data Layout  [II II II II PP TT F1 F2 F3]CC CC 8
    Sample        1c 50 f1 75 85 45 f8 02 03 73 42 8

- II:{32} ID, hexa 0x1c50f175 or decimal value 475066741
- PP:{8}: Tire pressure, PSI = (PP - 43) * 0.363 or kPa = ( PP - 43 ) * 2.5
- TT:{8}: Temperature in C offset 40
- F1, F2, F3: Flags that could contain battery information, flat tire, lost of pressure ...
- CC: CRC-16 bits, poly 0x1021, init 0x0000 [from previous 9 bytes].
- 8: useless trailing bit

*/

#include "decoder.h"

static int tpms_bmwg3_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_t decoded = { 0 };
    uint8_t *b;
    // preamble = 0xcccd
    uint8_t const preamble_pattern[] = {0xcc, 0xcd};

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 2, __func__, "row error");
        return DECODE_ABORT_EARLY;
    }

    int pos = 0;
    pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 16);
    if (pos >= bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 1, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    decoder_log_bitrow(decoder, 1, __func__, bitbuffer->bb[0], bitbuffer->bits_per_row[0], "MSG");

    bitbuffer_differential_manchester_decode(bitbuffer, 0, pos + sizeof(preamble_pattern) * 8, &decoded, 88); // 11 * 8

    decoder_log_bitrow(decoder, 2, __func__, decoded.bb[0], decoded.bits_per_row[0], "DMC");

    if (decoded.bits_per_row[0] < 88) {
        decoder_logf(decoder, 2, __func__, "Too short");
        return DECODE_ABORT_LENGTH;
    }

    b = decoded.bb[0];

    if (crc16(b, 11, 0x1021, 0x0000)) {
        decoder_logf(decoder, 1, __func__, "crc error, expected %02x%02x, calculated %04x", b[9], b[10], crc16(b, 11, 0x1021, 0x0000));
        return DECODE_FAIL_MIC; // crc mismatch
    }
    decoder_log(decoder, 1, __func__, "BMW G3 found");
    float pressure_kPa      = (b[4] - 43) * 2.5f;
    float temperature_C     = (b[5] - 40);
    int flags1              = b[6]; // fixed value to 0xf8 could be Brand ID ?
    int flags2              = b[7]; // Battery , pressure warning ?
    int flags3              = b[8]; // fixed value to 0x03 could be Brand ID ?

    uint32_t id             = ((uint32_t)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | (b[3]);

    char msg_str[23];
    snprintf(msg_str, sizeof(msg_str), "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10]);

    /* clang-format off */
    data_t *data = data_make(
            "model",               "",                DATA_STRING, "BMW-GEN3",
            "type",                "",                DATA_STRING, "TPMS",
            //"id",                  "",                DATA_FORMAT, "%08x",     DATA_INT,    id,
            "id",                  "",                DATA_INT,    id,
            "pressure_kPa",        "Pressure",        DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "temperature_C",       "Temperature",     DATA_FORMAT, "%.1f C",   DATA_DOUBLE, temperature_C,
            "flags1",              "",                DATA_FORMAT, "%08b",     DATA_INT,    flags1,
            "flags2",              "",                DATA_FORMAT, "%08b",     DATA_INT,    flags2,
            "flags3",              "",                DATA_FORMAT, "%08b",     DATA_INT,    flags3,
            "msg",                 "msg",             DATA_STRING, msg_str, // To remove after guess all tags
            "mic",                 "Integrity",       DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "battery_ok",
        "pressure_kPa",
        "flags1",
        "flags2",
        "flags3",
        "msg",
        "mic",
        NULL,
};

r_device const tpms_bmwg3 = {
        .name        = "BMW Gen3 TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 160,
        .decode_fn   = &tpms_bmwg3_decode,
        .fields      = output_fields,
};
