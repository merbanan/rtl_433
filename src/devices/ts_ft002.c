/** @file
    TS-FT002 Tank Liquid Level decoder.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/*
TS-FT002 Wireless Ultrasonic Tank Liquid Level Meter With Temperature Sensor

PPM with 500 us pulse, 464 us short gap (0), 948 us long gap (1), 1876 us packet gap, two packets per transmission.

E.g. rtl_433 -R 0 -X 'n=TS-FT002T,m=OOK_PPM,s=500,l=1000,g=1200,r=2000,bits>=70'

Bits are sent LSB first, full packet is 9 bytes (1 byte preamble + 8 bytes payload)

Data layout:

    SS II MM DD BD VT TT CC

(Nibble number after reflecting bytes)
| Nibble   | Description
| 0,1      | Sync 0xfa (0x5f before reverse)
| 2,3      | ID
| 4,5      | Message type (fixed 0x11)
| 6,7,9    | Depth H,M,L (in Centimeter, 0x5DC if invalid, range 0-15M)
| 8        | Transmit Interval (bit 7=0: 180S, bit 7 =1: 30S, bit 4-6=1: 5S)
| 10       | Battery indicator?
| 12,13,11 | Temp H, M, L (scale 10, offset 400), 0x3E8 if invalid
| 14,15    | Rain H, L (Value 0-256), not used
| 16,17    | XOR checksum (include the preamble)
*/

#include "decoder.h"

static int ts_ft002_decoder(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[9];
    int id, type, depth, transmit, temp_raw, batt_low;
    float temp_c;

    if (bitbuffer->bits_per_row[0] == 72) {
        bitbuffer_extract_bytes(bitbuffer, 0, 0, b, 72);
    }
    else if (bitbuffer->bits_per_row[0] == 71) {
        bitbuffer_extract_bytes(bitbuffer, 0, 7, b + 1, 64);
        b[0] = bitbuffer->bb[0][0] >> 1;
    }
    else if (bitbuffer->bits_per_row[0] == 70) {
        bitbuffer_extract_bytes(bitbuffer, 0, 6, b + 1, 64);
        b[0] = (bitbuffer->bb[0][0] >> 2) | 0x80;
    }
    else
        return DECODE_ABORT_LENGTH;

    int chk = xor_bytes(b, 9);
    if (chk)
        return DECODE_FAIL_MIC;

    // reflect bits (also reverses nibbles)
    reflect_bytes(b, 8);

    id       = b[1];
    type     = b[2];
    depth    = (b[3] << 4) | (b[4] & 0x0f);
    batt_low = b[4] >> 4;
    transmit = b[5] >> 4;
    temp_raw = (b[6] << 4) | (b[5] & 0x0f);
    //rain     = b[7];
    //chk      = b[8];
    temp_c   = (temp_raw - 400) * 0.1f;

    if ((transmit & 0x07) == 0x07)
        transmit = 5;
    else if ((transmit & 0x08) == 0x08)
        transmit = 30;
    else if (transmit == 0)
        transmit = 180;
    else // invalid
        transmit = 0;

    if (type != 0x11)
        return DECODE_FAIL_SANITY;

    /* clang-format off */
    data = data_make(
            "model",            "",                     DATA_STRING, "TS-FT002",
            "id",               "Id",                   DATA_INT,    id,
            "depth_cm",         "Depth",                DATA_INT,    depth,
            "temperature_C",    "Temperature",          DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "transmit_s",       "Transmit Interval",    DATA_INT,    transmit,
            //"battery_ok",       "Battery Flag",         DATA_INT,    batt_low,
            "flags",            "Battery Flag?",         DATA_INT,    batt_low,
            "mic",              "MIC",                  DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);
    /* clang-format on */

    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "depth_cm",
        "temperature_C",
        "transmit_s",
        //"battery_ok",
        "flags",
        "mic",
        NULL,
};

r_device ts_ft002 = {
        .name        = "TS-FT002 Wireless Ultrasonic Tank Liquid Level Meter With Temperature Sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 464,
        .long_width  = 948,
        .gap_limit   = 1200,
        .reset_limit = 2000,
        .decode_fn   = &ts_ft002_decoder,
        .disabled    = 0,
        .fields      = output_fields,
};
