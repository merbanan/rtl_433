/** @file
    TRW TPMS Sensor.

    Copyright (C) 2025 Bruno OCTAU \@ProfBoc75

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int tpms_trw_decode(r_device *decoder, bitbuffer_t *bitbuffer, int type)
TRW TPMS Sensor.

FCC-ID: GQ4-70T

- OEM and Chinese OEM models
- Used into Chrysler car from 2014 until 2022 : car models https://www.chryslertpms.com/chrysler-tpms-types-fitment

- OOK then FSK rf signals, mode detail here : https://fcc.report/FCC-ID/GQ4-70T/2201535

S.a issue #3256

 Data layout:

    Byte Position xx xx 0  1  2  3  4  5  6  7  8  9  10
    Sample OOK    00 01 5c 3e 52 85 2e 61 53 4b 0e 52 4
    Sample FSK    7f ff 5c 3e 52 85 2e 62 53 4b 0e 68 4
                   PRE  MM II II II II FN PP TT SS CC X

- PRE : 7FFF (FSK) or 0001 (OOK)

- M:{8}  Mode/Model : 0x5c, 0x5d or 0x5e (Always 0x5c for Chinese OEM model)
- I:{32} Sensor ID
- F:{4}  Flag status : 0x6 = pressure drop (OEM) , 0x9 pressure increase (OEM) or drop (Chinese OEM), 0xb or 0xc alternatly in Motion
- N:{4}  Seq number : 0,1,2,3,0,1,2,3... for OEM, 1,2,3,4,1,2,3,4... for Chinese OEM
- P:{8}  Pressure PSI, scale 2.5
- T:{8}  Temperature C, offset 50
- S:{8}  Motion status, 0x0e parked, other value in motion, not yet guesses
- C:{8}  CRC-8/SMBUS, poly 0x07, init 0x00, final XOR 0x00
- X:{4}  Trailing bit but 0x4 OEM, 0x0 Chinese OEM

*/

static unsigned char const preamble_ook[] = { 0x00, 0x01 };
static unsigned char const preamble_fsk[] = { 0x7f, 0xff };

static int tpms_trw_decode(r_device *decoder, bitbuffer_t *bitbuffer, int type)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int msg_len = bitbuffer->bits_per_row[0];

    if (msg_len > 98) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int pos = 0;

    if (type == 0) {
        pos = bitbuffer_search(bitbuffer, 0, pos, preamble_ook, sizeof(preamble_ook) * 8);
    }
    if (type == 1) {
        pos = bitbuffer_search(bitbuffer, 0, pos, preamble_fsk, sizeof(preamble_fsk) * 8);
    }

    if (pos >= bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    if (pos + 8 * 11 > bitbuffer->bits_per_row[0]) {
        decoder_log(decoder, 2, __func__, "Length check fail");
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[11]  = {0};
    pos += 16;

    if ((msg_len - pos) < 81 ) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, sizeof(b) * 8);

    if (crc8(b,10,0x07,0x00)) {
        decoder_logf(decoder, 1, __func__, "CRC Error, expected: %02x", crc8(b, 9, 0x07, 0x00));
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 0, __func__, b, 88, "MSG");

    int mode               = b[0];
    int id                 = b[1] << 24 | b[2] << 16 | b[3] << 8 | b[4];
    int flags              = (b[5] & 0xF0) >> 4;
    int seq_num            = b[5] & 0x0F;
    int pressure_raw       = b[6];
    float pressure_psi     = pressure_raw * 0.4f;
    int temp_raw           = b[7];
    int temperature_C      = temp_raw - 50;
    int motion_flags       = b[8];
    int oem_model          = (b[10] & 0xf0) >> 4;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "TRW",
            "type",             "",             DATA_STRING, "TPMS",
            "mode",             "",             DATA_FORMAT, "%02x",     DATA_INT, mode,
            "id",               "",             DATA_FORMAT, "%08x",     DATA_INT, id,
            //"battery_ok",       "Battery_OK",   DATA_INT,    !low_batt,
            "flags",            "Flags",        DATA_FORMAT, "%01x",     DATA_INT, flags,
            "alert",            "Alert",        DATA_COND, flags == 0x6 || flags == 0x9, DATA_STRING, "Pressure increase/decrease !",
            "seq_num",          "Seq Num",                               DATA_INT, seq_num,
            "pressure_PSI",     "Pressure",     DATA_FORMAT, "%.0f PSI", DATA_DOUBLE, pressure_psi,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C",   DATA_DOUBLE, (double)temperature_C,
            "motion_flags",     "Motion flags", DATA_FORMAT, "%02x",     DATA_INT, motion_flags,
            "motion_status",    "Motion",       DATA_STRING, motion_flags == 0x0e ? "Parked" : "Moving",
            //"inflate",          "Inflate",      DATA_INT,    infl_detected,
            "oem_model",        "OEM Model",    DATA_COND,   oem_model == 0x4, DATA_STRING, "OEM",
            "oem_model",        "OEM Model",    DATA_COND,   oem_model == 0x0, DATA_STRING, "Chinese OEM",
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
TRW TPMS Sensor.
@sa tpms_trw_decode()
*/
static int tpms_trw_callback_ook(r_device *decoder, bitbuffer_t *bitbuffer)
{
    return tpms_trw_decode(decoder, bitbuffer, 0);
}

/**
TRW TPMS Sensor.
@sa tpms_trw_decode()
*/
static int tpms_trw_callback_fsk(r_device *decoder, bitbuffer_t *bitbuffer)
{
    return tpms_trw_decode(decoder, bitbuffer, 1);
}

static char const *const output_fields[] = {
        "model",
        "type",
        "mode",
        "id",
        //"battery_ok",
        "pressure_kPa",
        "temperature_C",
        "seq_num",
        "flags",
        "motion_flags",
        //"fast_leak",
        //"inflate",
        "oem_model",
        "mic",
        NULL,
};

r_device const tpms_trw_ook = {
        .name        = "TRW TPMS OOK OEM and Chinese models",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 200,
        .decode_fn   = &tpms_trw_callback_ook,
        .fields      = output_fields,
};

r_device const tpms_trw_fsk = {
        .name        = "TRW TPMS FSK OEM and Chinese models",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 200,
        .decode_fn   = &tpms_trw_callback_fsk,
        .fields      = output_fields,
};
