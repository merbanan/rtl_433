/** @file
    BMW Gen4 Gen5 TPMS and Audi TPMS Pressure Alert sensor.

    Copyright (C) 2024 Bruno OCTAU (ProfBoc75), \@petrjac, \@Gucioo, Christian W. Zuckschwerdt <christian@zuckschwerdt.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
BMW Gen4 Gen5 TPMS and Audi TPMS Pressure Alert sensor.

issue #2821:
- BMW Gen5 TPMS support open by \@petrjac
- BMW Gen4 TPMS supported

#2821 issue comment 2043641606 \@Gucioo
- Audi TPMS based on the same protocol with shorter message in case of sudden increase or decrease in pressure

Samples raw :

    BMW
    {207}555554b2aab4b2b552acb4d332accb32b552aaacd334d32ad334
    {211}555554b2aab4b2b552acb4d332acb4cab54caaacd4cad32b4b55e

    Audi Pressure Alert
    {166}2aaaaa5955555955a5556a65666a56aa65a65999fc
    {165}2aaaaa5955555955a5556a65666a56aa65a65999f8
    {167}5555552caaaaacaad2aab532b3352b5532d32cccfe

- Preamble {16} 0xaa59 before MC
- MC Zero bit coded, 11 bytes or 8 bytes

Samples after MC Inverted:

    BMW
     0  1  2  3  4  5  6  7  8  9 10
    MM II II II II PP TT F1 F2 F3 CC
    03 23 e1 36 a1 4a 3e 01 6b 68 6b
    03 23 e1 36 a1 34 3d 01 74 68 cf

    AUDI Pressure Alert
     0  1  2  3  4  5  6  7
    MM II II II II PP TT CC
    00 20 c0 74 57 36 4c 23

- MM : Brand BRAND ID, 0x00 = Audi Pressure Alert, 0x03 = HUF Gen 5/Beru, 0x23 = Schrader/Sensata, 0x80 = Continental, 0x88 Audi
- II : Sensor ID
- PP : Pressure * 2.45 kPa
- TT : Temp - 52 C
- F1 : BMW only, Warning Flags , battery, fast deflating ... not yet guess
- F2 : BMW only, Sequence number, to be confirmed
- F3 : BMW only, Target Nominal Pressure * 0.0245 for 0x03
- CC : CRC 8 of previous bytes (7 bytes for Audi Pressure Alert, 10 bytes for BMW) , poli 0x2f, init 0xaa

Data layout after MC for HUF Gen 5:

    BRAND = 8h | SENSOR_ID = 32h      | PRESS = 8d  | TEMP = 8d  | FLAGS1 = 8h | FLAGS2 = 8h | FLAGS3 = 8d  | CRC = 8h

    BRAND = 03 | SENSOR_ID = 23e136a1 | PRESS = 074 | TEMP = 062 | FLAGS1 = 01 | FLAGS2 = 6b | FLAGS3 = 104 | CRC = 6b
    BRAND = 03 | SENSOR_ID = 23e136a1 | PRESS = 052 | TEMP = 061 | FLAGS1 = 01 | FLAGS2 = 74 | FLAGS3 = 104 | CRC = cf

Continental model:

    F1, F2, F3 to guess

Schrader/Sensata model:

    F1, F2, F3 to guess

Audi Pressure Alert:

    BRAND = 8h | SENSOR_ID = 32h      | PRESS = 8d  | TEMP = 8d  | CRC = 8h

    BRAND = 00 | SENSOR_ID = 20c07457 | PRESS = 054 | TEMP = 076 | CRC = 6b

*/

#include "decoder.h"

static int tpms_bmw_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_t decoded = { 0 };
    uint8_t *b;
    // preamble is aa59
    uint8_t const preamble_pattern[] = {0xaa, 0x59};
    uint8_t len_msg = 11; // default for BMW = 11, if Audi-Alert len_msg = 8
    int flags1      =  0;
    int flags2      =  0;
    int flags3      =  0;
    char msg_str[23];

    if (bitbuffer->num_rows != 1) {
        decoder_logf(decoder, 2, __func__, "row error");
        return DECODE_ABORT_EARLY;
    }

    int pos = 0;
    pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);
    if (pos >= bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    decoder_log_bitrow(decoder, 2, __func__, bitbuffer->bb[0], bitbuffer->bits_per_row[0], "MSG");

    bitbuffer_manchester_decode(bitbuffer, 0, pos + sizeof(preamble_pattern) * 8, &decoded, len_msg * 8);

    decoder_log_bitrow(decoder, 2, __func__, decoded.bb[0], decoded.bits_per_row[0], "MC");

    if (decoded.bits_per_row[0] < 88) {
        // Check if Audi
        if (decoded.bits_per_row[0] >= 64 ) {
            len_msg = 8;
        }
        else {
            decoder_logf(decoder, 1, __func__, "Too short");
            return DECODE_ABORT_LENGTH;
        }
    }

    bitbuffer_invert(&decoded); // MC Zerobit
    decoder_log_bitrow(decoder, 2, __func__, decoded.bb[0], decoded.bits_per_row[0], "MC inverted");
    b = decoded.bb[0];
    if (crc8(b, len_msg, 0x2f, 0xaa)) {
        decoder_logf(decoder, 1, __func__, "crc error, expected %02x, calculated %02x", b[11], crc8(b, len_msg, 0x2f, 0xaa));
        return DECODE_FAIL_MIC; // crc mismatch
    }
    decoder_log(decoder, 2, __func__, "BMW or Audi found");
    int brand_id            = b[0]; // 0x00 = Audi-Alert, 0x03 = HUF/Beru, 0x23 = Schrader/Sensata, 0x80 = Continental, 0x88 = Audi
    float pressure_kPa      = b[5] * 2.45;
    int temperature_C       = b[6] - 52;

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x%02x", b[1], b[2], b[3], b[4]);

    if (len_msg == 11) {
        flags1              = b[7]; // depends on brand_id, could be pressure or SEQ ID and other WARM flags Battery , fast deflating ...
        flags2              = b[8]; // depends on brand_id, could be pressure and other WARM flags Battery , fast deflating ...
        flags3              = b[9]; // Nominal Pressure for brand HUF 0x03, depends on brand_id, could be SEQ ID and other WARM flags Battery , fast deflating ...
        snprintf(msg_str, sizeof(msg_str), "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10]);
    }
    else {
        snprintf(msg_str, sizeof(msg_str), "%02x%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",               "",                DATA_COND, len_msg == 11, DATA_STRING, "BMW-GEN5",
            "model",               "",                DATA_COND, len_msg == 8, DATA_STRING, "Audi-PressureAlert",
            "type",                "",                DATA_STRING, "TPMS",
            "alert",               "Alert",           DATA_COND, len_msg == 8, DATA_STRING, "Alert Pressure increase/decrease !",
            "brand",               "Brand",           DATA_INT,    brand_id,
            "id",                  "",                DATA_STRING, id_str,
            "pressure_kPa",        "Pressure",        DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "temperature_C",       "Temperature",     DATA_FORMAT, "%.1f C",   DATA_DOUBLE, (double)temperature_C,
            "flags1",              "",                DATA_COND, len_msg == 11, DATA_INT,    flags1,
            "flags2",              "",                DATA_COND, len_msg == 11, DATA_INT,    flags2,
            "flags3",              "",                DATA_COND, len_msg == 11, DATA_INT,    flags3, // Nominal Pressure for brand HUF 0x03
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
        "alert",
        "id",
        "brand",
        "battery_ok",
        "pressure_kPa",
        "flags1",
        "flags2",
        "flags3",
        "msg",
        "mic",
        NULL,
};

r_device const tpms_bmw = {
        .name        = "BMW Gen4-Gen5 TPMS and Audi TPMS Pressure Alert, multi-brand HUF/Beru, Continental, Schrader/Sensata, Audi",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 25,
        .long_width  = 25,
        .reset_limit = 100,
        .decode_fn   = &tpms_bmw_decode,
        .fields      = output_fields,
};
