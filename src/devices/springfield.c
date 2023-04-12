/** @file
    Springfield PreciseTemp Wireless Temperature and Soil Moisture Station.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Springfield PreciseTemp Wireless Temperature and Soil Moisture Station.

Note: this is a false positive for AlectoV1.

http://www.amazon.com/Springfield-Digital-Moisture-Meter-Freeze/dp/B0037BNHLS

Data is transmitted in 9 nibbles

    [id0] [id1] [flags] [temp0] [temp1] [temp2] [moist] [chk] [unkn]

- id: 8 bit a random id that is generated when the sensor starts
- flags(3): Battery low flag, 1 when the battery is low, otherwise 0 (ok)
- flags(2): TX Button Pushed, 1 when the sensor sends a reading while pressing the button on the sensor
- flags(1,0): Channel number that can be set by the sensor (1, 2, 3, X)
- temp: 12 bit Temperature Celsius x10 in 3 nibbles 2s complement
- moist: 4 bit Moisture Level of 0 - 10
- chk: 4 bit Checksum of nibbles 0 - 6 (simple xor of nibbles)
- unkn: 4 bit Unknown

Actually 37 bits for all but last transmission which is 36 bits.
*/

#include "decoder.h"

static int springfield_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = 0;
    unsigned tmpData;
    unsigned savData = 0;

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] != 36 && bitbuffer->bits_per_row[row] != 37)
            continue; // DECODE_ABORT_LENGTH
        uint8_t *b = bitbuffer->bb[row];
        tmpData = ((unsigned)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
        if (tmpData == 0xffffffff)
            continue; // DECODE_ABORT_EARLY
        if (tmpData == savData)
            continue;
        savData = tmpData;

        int chk = xor_bytes(b, 4); // sum nibble 0-7
        chk = (chk >> 4) ^ (chk & 0x0f); // fold to nibble
        if (chk != 0)
            continue; // DECODE_FAIL_MIC

        int sid      = (b[0]);
        int battery  = (b[1] >> 7) & 1;
        int button   = (b[1] >> 6) & 1;
        int channel  = ((b[1] >> 4) & 0x03) + 1;
        int temp     = (int16_t)(((b[1] & 0x0f) << 12) | (b[2] << 4)); // uses sign extend
        float temp_c = (temp >> 4) * 0.1f;
        int moisture = (b[3] >> 4) * 10; // Moisture level is 0-10
        //int uk1      = b[4] >> 4; /* unknown. */

        // reduce false positives by checking specified sensor range, this isn't great...
        if (temp_c < -30 || temp_c > 70) {
            decoder_logf(decoder, 2, __func__, "temperature sanity check failed: %.1f C", temp_c);
            return DECODE_FAIL_SANITY;
        }

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",             DATA_STRING, "Springfield-Soil",
                "id",               "SID",          DATA_INT,    sid,
                "channel",          "Channel",      DATA_INT,    channel,
                "battery_ok",       "Battery",      DATA_INT,    !battery,
                "transmit",         "Transmit",     DATA_STRING, button ? "MANUAL" : "AUTO", // TODO: delete this
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                "moisture",         "Moisture",     DATA_FORMAT, "%d %%", DATA_INT, moisture,
                "button",           "Button",       DATA_INT,    button,
//                "uk1",            "uk1",          DATA_INT,    uk1,
                "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        ret++;
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "transmit", // TODO: delete this
        "temperature_C",
        "moisture",
        "button",
        "mic",
        NULL,
};

r_device const springfield = {
        .name        = "Springfield Temperature and Soil Moisture",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 5000,
        .reset_limit = 9200,
        .decode_fn   = &springfield_decode,
        .priority    = 10, // Alecto collision, if Alecto checksum is correct it's not Springfield-Soil
        .fields      = output_fields,
};
