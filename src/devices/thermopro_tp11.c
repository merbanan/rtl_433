/* Thermopro TP-11 Thermometer.
 *
 * Copyright (C) 2017 Google Inc.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include "data.h"
#include "rtl_433.h"
#include "util.h"

#define MODEL "Thermopro TP11 Thermometer"

/* normal sequence of bit rows:
[00] {33} db 41 57 c2 80 : 11011011 01000001 01010111 11000010 1
[01] {33} db 41 57 c2 80 : 11011011 01000001 01010111 11000010 1
[02] {33} db 41 57 c2 80 : 11011011 01000001 01010111 11000010 1
[03] {32} db 41 57 c2 : 11011011 01000001 01010111 11000010 

The code below checks that at least three rows are the same and
that the validation code is correct for the known device ids.
*/

static int valid(unsigned data, unsigned check) {
    // This table is computed for device ids 0xb34 and 0xdb4. Since the code
    // appear to be linear, it is most likely correct also for device ids
    // 0 and 0xb34^0xdb4 == 0x680. It needs to be updated for others, the
    // values starting at table[12] are most likely wrong for other devices.
    static int table[] = {
        0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x51, 0xa2, 
        0x15, 0x2a, 0x54, 0xa8, 0x00, 0x00, 0xed, 0x00,
        0x00, 0x00, 0x00, 0x37, 0x00, 0x00, 0x00, 0x00};
    for(int i=0;i<24;i++) {
        if (data & (1 << i)) check ^= table[i];
    }
    return check == 0;
}

static int thermopro_tp11_sensor_callback(bitbuffer_t *bitbuffer) {
    int i, j, iTemp, good = -1;
    float fTemp;
    bitrow_t *bb = bitbuffer->bb;
    unsigned int device, value;
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;

    // Compare first four bytes of rows that have 32 or 33 bits.
    for (i = 0; good < 0 && i + 2 < bitbuffer->num_rows; i++) {
        int equal_rows = 0;
        if ((bitbuffer->bits_per_row[i] & ~1) != 32) continue;

        for (j = i+1; good < 0 && j < bitbuffer->num_rows; j++) {
            if ((bitbuffer->bits_per_row[j] & ~1) == 32
                && bb[i][0] == bb[j][0]
                && bb[i][1] == bb[j][1]
                && bb[i][2] == bb[j][2]
                && bb[i][3] == bb[j][3]
                && ++equal_rows >= 2) good = i;
        }
    }
    if (good < 0) return 0;

    // bb[good] is equal to at least two other rows, decode.
    value = (bb[good][0] << 16) + (bb[good][1] << 8) + bb[good][2];
    device = value >> 12;
    // Validate code for known devices.
    if ((device == 0xb34 || device == 0xdb4 ) && !valid(value, bb[good][3]))
        return 0;
    iTemp = value & 0xfff;
    fTemp = (iTemp - 200) / 10.;

    local_time_str(0, time_str);
    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, MODEL,
                     "id",            "Id",          DATA_FORMAT, "\t %d",   DATA_INT,    device,
                     "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, fTemp,
                     NULL);
    data_acquired_handler(data);
    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "temperature_C",
    NULL
};

r_device thermopro_tp11 = {
    .name          = MODEL,
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = 956,
    .long_limit    = 1912,
    .reset_limit   = 3880,
    .json_callback = &thermopro_tp11_sensor_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields,
};
