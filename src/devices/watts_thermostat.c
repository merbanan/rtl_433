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

/** @fn int watts_thermostat_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Watts WFHT-RF Thermostat.

This code is based on a slightly older OEM system created by ADEV in France which
later merged with Watts. The closest thing currently available seems to be
https://wattswater.eu/catalog/regulation-and-control/radio-wfht-thermostats/electronic-room-thermostat-with-rf-control-wfht-rf-basic/,
but it is not known whether they are protocol compatible.

Modulation is PWM with preceeding gap. There is a very long lead-in pulse.
Symbols are ~260 us gap + ~600 us pulse and ~600 us gap + ~260 us pulse.
Bits are inverted and reflected.

Example Data:

    10100101   1011010001110110   1000   100100001   000011000   10101011
    preamble   id                 flags  temp         setpoint   chksum

Data Layout:

    PP II II F .TT .SS XX

- P: (8-bit reflected) Preamble
- I: (16-bit reflected) ID
- F: (4-bit reflected) Flags
- T: (9-bit reflected) Temperature
- S: (9-bit reflected) Set-Point
- X: (8-bit reflected) Checksum (8-bit sum)

    The only flag found is PAIRING (0b0001). Chksum is calculated by summing all
    high and low bytes the for ID, Flags, Temperature and Set-Point.

    Temperature and Set-Point values are in 0.1°C steps with an observed Set-Point
    range of ~4°C to ~30°C.

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

    https://tinyurl.com/wattsthermobitbench

    Format string:
    PRE:^8h ID:^16d FLAGS:^4b TEMP:^9d SETP:^9d CHK:^8d

Decoded example:

    PRE:a5 ID:28082 FLAGS:0001 TEMP:271 SETP:304 CHK:097
    PRE:a5 ID:28252 FLAGS:0000 TEMP:019 SETP:303 CHK:013

*/

#define WATTSTHERMO_BITLEN             54
#define WATTSTHERMO_PREAMBLE_BITLEN    8
#define WATTSTHERMO_ID_BITLEN          16
#define WATTSTHERMO_FLAGS_BITLEN       4
#define WATTSTHERMO_TEMPERATURE_BITLEN 9
#define WATTSTHERMO_SETPOINT_BITLEN    9
#define WATTSTHERMO_CHKSUM_BITLEN      8

enum WATTSTHERMO_FLAGS {
    WF_NONE     = 0,
    WF_PAIRING  = 1,
    WF_UNKNOWN1 = 2,
    WF_UNKNOWN2 = 4,
    WF_UNKNOWN3 = 8,
};

static int watts_thermostat_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xa5}; // inverted, raw value is 0x5a

    bitbuffer_invert(bitbuffer);

    // We're expecting a single row
    for (uint16_t row = 0; row < bitbuffer->num_rows; ++row) {
        uint16_t row_len = bitbuffer->bits_per_row[row];

        unsigned bitpos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, WATTSTHERMO_PREAMBLE_BITLEN);
        if (bitpos >= row_len) {
            decoder_log(decoder, 2, __func__, "Preamble not found");
            return DECODE_ABORT_EARLY;
        }

        if (bitpos + WATTSTHERMO_BITLEN > row_len) {
            decoder_log(decoder, 2, __func__, "Message too short");
            return DECODE_ABORT_LENGTH;
        }
        bitpos += WATTSTHERMO_PREAMBLE_BITLEN;

        uint8_t id_raw[2];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, id_raw, WATTSTHERMO_ID_BITLEN);
        reflect_bytes(id_raw, 2);
        int id = (id_raw[1] << 8) | id_raw[0];
        bitpos += WATTSTHERMO_ID_BITLEN;

        uint8_t flags[1];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, flags, WATTSTHERMO_FLAGS_BITLEN);
        reflect_bytes(flags, 1);
        int pairing = flags[0] & WF_PAIRING;
        bitpos += WATTSTHERMO_FLAGS_BITLEN;

        uint8_t temp_raw[2];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, temp_raw, WATTSTHERMO_TEMPERATURE_BITLEN);
        reflect_bytes(temp_raw, 2);
        int temp = (temp_raw[1] << 8) | temp_raw[0];
        bitpos += WATTSTHERMO_TEMPERATURE_BITLEN;

        uint8_t setp_raw[2];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, setp_raw, WATTSTHERMO_SETPOINT_BITLEN);
        reflect_bytes(setp_raw, 2);
        int setp = (setp_raw[1] << 8) | setp_raw[0];
        bitpos += WATTSTHERMO_SETPOINT_BITLEN;

        uint8_t chksum = add_bytes(id_raw, 2)
                + add_bytes(flags, 1)
                + add_bytes(temp_raw, 2)
                + add_bytes(setp_raw, 2);

        uint8_t chk[1];
        bitbuffer_extract_bytes(bitbuffer, row, bitpos, chk, WATTSTHERMO_CHKSUM_BITLEN);
        reflect_bytes(chk, 1);
        if (chk[0] != chksum) {
            decoder_log_bitbuffer(decoder, 1, __func__, bitbuffer, "Checksum fail");
            return DECODE_FAIL_MIC;
        }

        if (id == 0 && flags[0] == 0 && temp == 0 && setp == 0 && chk[0] == 0) {
            decoder_log(decoder, 2, __func__, "Rejecting false positive");
            return DECODE_ABORT_EARLY;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",            "Model",            DATA_STRING, "Watts-WFHTRF",
                "id",               "ID",               DATA_INT,    id,
                "pairing",          "Pairing",          DATA_INT,    pairing,
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C",      DATA_DOUBLE,  temp * 0.1f,
                "setpoint_C",       "Setpoint",         DATA_FORMAT, "%.1f C",      DATA_DOUBLE,  setp * 0.1f,
                "flags",            "Flags",            DATA_INT,    flags[0],
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
