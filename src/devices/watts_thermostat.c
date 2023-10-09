/** @file
    Watts WFHT-RF Thermostat.

    Copyright (C) 2022 Ådne Hovda <aadne@hovda.no>
    based on protocol decoding by Christian W. Zuckschwerdt <zany@triq.net>
    and Ådne Hovda <aadne@hovda.no>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"
/**
Watts WFHT-RF Thermostat.

This code is based on a slightly older OEM system created by ADEV in France which
later merged with Watts. The closest thing currently available seems to be
https://wattswater.eu/catalog/regulation-and-control/radio-wfht-thermostats/electronic-room-thermostat-with-rf-control-wfht-rf-basic/,
but it is not known whether they are protocol compatible.

Analyzer output:
    Analyzing pulses...
    Total count:   55,  width: 337.75 ms            (675502 S)
    Pulse width distribution:
     [ 0] count:    1,  width: 291096 us [291096;291096]    (582191 S)
     [ 1] count:   26,  width:  630 us [630;640]    (1260 S)
     [ 2] count:   28,  width:  288 us [286;302]    ( 576 S)
    Gap width distribution:
     [ 0] count:   26,  width:  233 us [232;234]    ( 466 S)
     [ 1] count:   28,  width:  576 us [576;576]    (1151 S)
    Pulse period distribution:
     [ 0] count:    1,  width: 291328 us [291328;291328]    (582657 S)
     [ 1] count:   15,  width: 1206 us [1205;1216]  (2412 S)
     [ 2] count:   14,  width:  522 us [520;530]    (1043 S)
     [ 3] count:   24,  width:  864 us [862;877]    (1727 S)
    Pulse timing distribution:
     [ 0] count:    1,  width: 291096 us [291096;291096]    (582191 S)
     [ 1] count:   54,  width:  602 us [576;640]    (1204 S)
     [ 2] count:   54,  width:  262 us [232;302]    ( 523 S)
     [ 3] count:    1,  width: 100000 us [100000;100000]    (200001 S)
    Level estimates [high, low]:  15961,    150
    RSSI: -0.1 dB SNR: 20.3 dB Noise: -20.4 dB
    Frequency offsets [F1, F2]:   17869,      0     (+545.3 kHz, +0.0 kHz)
    Guessing modulation: Pulse Width Modulation with sync/delimiter
    view at https://triq.org/pdv/#AAB104FFFF025A0105FFFF8291A291A1A291A29291A29291A291A1A1A2929291A29291A1A1A1A1A29291A291A1A1A1A291A1A29291A1A29291A29291A1A2929291A355
    Attempting demodulation... short_width: 288, long_width: 630, reset_limit: 577, sync_width: 291096
    Use a flex decoder with -X 'n=name,m=OOK_PWM,s=288,l=630,r=577,g=0,t=0,y=291096'
    pulse_slicer_pwm(): Analyzer Device
    bitbuffer:: Number of rows: 1
    [00] {54} 5a 4b 89 f2 f6 64 c4


Data Layout:

    10100101   1011010001110110   1000   100100001   000011000   10101011
    preamble   id                 flags  temp         setpoint   chksum

    All fields need reflection, possibly easier to just reverse the whole
    row first. The only flag found is PAIRING (0b0001). Chksum seems to be
    additive.

Raw data:

    {54}5ab24971f79994
    {54}5ab24971f79994
    {54}5ab249f1f79b94
    {54}5ab249f1f79b94
    {54}5ab249f9f79854
    {54}5ab249f5f79a54
    {54}5ab249f68f998c
    {54}5ab249f98f9a4c
    {54}5ab249f58b9a4c
    {54}5ab249fb8f9acc

    https://tinyurl.com/2z5jtfuu

    Format string:
    ID:^16d FLAGS:^4b TEMP:^9d SETP:^9d CHK:^8d

Decoded example:

    ID:28205 FLAGS:0001 TEMP:265 SETP:048 CHK:214

*/

#define WATTSTHERMO_BITLEN             54
#define WATTSTHERMO_PREAMBLE           0xa5
#define WATTSTHERMO_PREAMBLE_BITLEN    8
#define WATTSTHERMO_ID_BITLEN          16
#define WATTSTHERMO_FLAGS_BITLEN       4
#define WATTSTHERMO_TEMPERATURE_BITLEN 9
#define WATTSTHERMO_SETPOINT_BITLEN    9
#define WATTSTHERMO_CHKSUM_BITLEN      8

enum WATTSTHERMO_FLAGS {
    NONE     = 0,
    PAIRING  = 1,
    UNKNOWN1 = 2,
    UNKNOWN2 = 4,
    UNKNOWN3 = 8,
};

static int watts_thermostat_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    // We're expecting a single row
    for (uint16_t row = 0; row < bitbuffer->num_rows; ++row) {

        uint16_t row_len    = bitbuffer->bits_per_row[row];
        uint8_t chksum      = 0;
        uint8_t preamble    = WATTSTHERMO_PREAMBLE;
        unsigned bitpos     = 0;

        bitpos = bitbuffer_search(bitbuffer, row, 0, &preamble, WATTSTHERMO_PREAMBLE_BITLEN);
        if (bitpos >= row_len) {
            decoder_log(decoder, 2, __func__, "Preamble not found");
            return DECODE_ABORT_EARLY;
        }

        if (row_len < WATTSTHERMO_BITLEN) {
            decoder_log(decoder, 2, __func__, "Message too short");
            return DECODE_ABORT_LENGTH;
        }
        bitpos += WATTSTHERMO_PREAMBLE_BITLEN;

        uint8_t id_raw[2];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, id_raw, WATTSTHERMO_ID_BITLEN);
        reflect_bytes(id_raw, 2);
        chksum += add_bytes(id_raw, 2);
        uint16_t id  = id_raw[1] << 8 | id_raw[0];
        bitpos += WATTSTHERMO_ID_BITLEN;

        uint8_t flags[1];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, flags, WATTSTHERMO_FLAGS_BITLEN);
        reflect_bytes(flags, 1);
        chksum += add_bytes(flags, 1);
        uint8_t pairing = flags[0] & PAIRING;
        bitpos += WATTSTHERMO_FLAGS_BITLEN;

        uint8_t temp_raw[2];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, temp_raw, WATTSTHERMO_TEMPERATURE_BITLEN);
        reflect_bytes(temp_raw, 2);
        chksum += add_bytes(temp_raw, 2);
        uint16_t temp = temp_raw[1] << 8 | temp_raw[0];
        bitpos += WATTSTHERMO_TEMPERATURE_BITLEN;

        uint8_t setp_raw[2];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, setp_raw, WATTSTHERMO_SETPOINT_BITLEN);
        reflect_bytes(setp_raw, 2);
        chksum += add_bytes(setp_raw, 2);
        uint16_t setp = setp_raw[1] << 8 | setp_raw[0];
        bitpos += WATTSTHERMO_SETPOINT_BITLEN;

        uint8_t chk[1];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, chk, WATTSTHERMO_CHKSUM_BITLEN);
        reflect_bytes(chk, 1);
        if (chk[0] != chksum) {
            decoder_log_bitbuffer(decoder, 1, __func__, bitbuffer, "Checksum fail.");
            return DECODE_FAIL_MIC;
        }

        /* clang-format off */
        data_t *data = data_make(
            "model",            "Model",            DATA_STRING, "Watts-WFHTRF",
            "id",               "ID",               DATA_INT,    id,
            "pairing",          "Pairing",          DATA_INT,    pairing,
            "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C",      DATA_DOUBLE,  temp * 0.1f,
            "setpoint_C",       "Setpoint",         DATA_FORMAT, "%.1f C",      DATA_DOUBLE,  setp * 0.1f,
            "flags",            "Flags",            DATA_INT,     flags,
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "pairing",
        "temperature_C",
        "setpoint_C",
        "flags",
        "mic",
        NULL,
};

r_device const watts_thermostat = {
        .name        = "Watts WFHT-RF Thermostat",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 260,
        .long_width  = 600,
        .sync_width  = 6000,
        .reset_limit = 900,
        .decode_fn   = &watts_thermostat_decode,
        .fields      = output_fields,
};