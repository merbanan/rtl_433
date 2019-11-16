/* 3 zones heater programer
 * Equation/Siemens ADLM FPRF on 433.863MHz
 *
 * A 50ms wakeup pulse followed by a 5ms gap,
 * then a start pulse 5ms gap + 3ms pulse followed by 41 data pulses.
 * This is repeated 3 times with the next wakeup directly following
 * the preceding stop pulses.
 *
 * Bit width is 2000 us with
 * Short pulse: ___- 1500us gap +  500 us pulse
 * Long pulse:  _---  500us gap + 1500 us pulse
 *
 *
 * Copyright (C) 2019 Pierre Tardy <tardyp@gmail.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static int adlm_fprf_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int r;
    int zone_id;
    int zone_nb;
    char *mode;
    char code_str[11];

    if (bitbuffer->num_rows < 3)
        return 0; // truncated transmission

    r = bitbuffer_find_repeated_row(bitbuffer, 3, 24);
    if (r < 0)
        return 0;

    // frame too short: false positive
    if (bitbuffer->bits_per_row[r] < 4*8)
        return 0;

    b = bitbuffer->bb[r];

    if (b[0]!= 0x40) // first byte always 0x40
        return 0;

    zone_id = b[1]<<8 | b[2];
    zone_nb = b[3] & 0xf;
    switch(b[4]>>4){
        case 0x9:
            mode = "ECO";
            break;
        case 0xa:
            mode = "CONFORT";
            break;
        case 0x8:
            mode = "OFF";
            break;
        default:
            return 0;
    }

    data = data_make(
        "model",         "",               DATA_STRING, _X("ADLM FPRF","Equation ADLM FPRF"),
        "id",            "Zone ID",        DATA_INT, zone_id,
        "zone",          "Zone number",    DATA_INT, zone_nb,
        "mode",          "Mode",           DATA_STRING, mode,
        NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "id",
    "zone",
    "mode",
    NULL
};

r_device adlm_fprf = {
    .name           = "3 zone heater programmer ADLM FPRF",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 500, // Threshold between short and long pulse [us]
    .long_width     = 1500,
    .gap_limit      = 2000, // Maximum gap size before new row of bits [us]
    .reset_limit    = 7000, // Maximum gap size before End Of Message [us]
    .decode_fn      = &adlm_fprf_callback,
    .disabled       = 0,
    .fields         = output_fields
};
