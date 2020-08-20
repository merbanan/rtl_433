/** @file
    Decoder for TFA-Twin-Plus-30.3221
    
    Copyright (C) 2020 Andriy Kulchytskyy
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3 as
    published by the Free Software Foundation.
    
*/

/**
Based on a decoder for TFA-Twin-Plus-30.3049

Data layout:

IIIIIIII B?CC11ST TTTTTTTT 1HHHHHHH ???????? ?

- I: sensor ID (changes on battery change)
- C: channel number
- B: low battery
- T: temperature
- S: sign
- X: checksum
- ?: unknown meaning
- 1: always 1
- it seems that the last byte contains checksum, but I didn't find a way  how to calculate it

All values have inverted data.

Example data:
[00] {41} da 9d 0f c2 54 80 : 11011010 10011101 00001111 11000010 01010100 1
[00] {41} e0 9d 0b c1 7d 80 : 11100000 10011101 00001011 11000001 01111101 1


*/

#include "decoder.h"

static int tfa_twin_plus_303221_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int row;
    uint8_t *b;

    row = bitbuffer_find_repeated_row(bitbuffer, 2, 41);
    
    if (row < 0){
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[row] != 41){
        return DECODE_ABORT_LENGTH;
    }

    b = bitbuffer->bb[row];

    if (!(b[0] || b[1] || b[2] || b[3] || b[4] || b[5])) /* exclude all zeros */{
        return DECODE_ABORT_EARLY;
    }
        
    int negative_sign = b[1] & 2;		
    int temp_msb = ((b[1] & 1) ^ 1) << 8;
    int temp_lsb = b[2] ^ 0xff;
    int temp = temp_msb | temp_lsb;
    
    int humidity = (b[3] & 0x7f) ^ 0x7f;
    
    int sensor_id     = b[0] ^ 0xff;
    int battery_low   = (b[1] & 0x80 >> 7) ^ 1;
    int channel       = (((b[1]>>4) & 3) ^ 3) + 1;
    
    float tempC = (negative_sign ? -( (1<<9) - temp ) : temp ) * 0.1F;

    data = data_make(
            "model",         "",            DATA_STRING, _X("TFA-TwinPlus","TFA-Twin-Plus-30.3221"),
            "id",            "Id",          DATA_INT, sensor_id,
            "channel",       "Channel",     DATA_INT, channel,
            "battery",       "Battery",     DATA_STRING, battery_low ? "LOW" : "OK",
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, tempC,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    "humidity",
    "mic",
    NULL
};

r_device tfa_twin_plus_303221 = {
    .name          = "TFA-Twin-Plus-30.3221",
    .modulation    = OOK_PULSE_PWM,
    .short_width   = 228,	
    .long_width    = 472,	
    .gap_limit     = 0,		
    .reset_limit   = 872,	
    .sync_width    = 828,	
    .decode_fn     = &tfa_twin_plus_303221_callback,
    .disabled      = 0,
    .fields         = output_fields
};
