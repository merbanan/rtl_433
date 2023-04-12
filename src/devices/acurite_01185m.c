/** @file
    Decoder for Acurite Grill/Meat Thermometer 01185M.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
    Based on work by Joe "exeljb"

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Decoder for Acurite Grill/Meat Thermometer 01185M.

Modulation:

- 56 bit PWM data
- short is 840 us pulse, 2028 us gap
- long is 2070 us pulse, 800 us gap,
- sync is 6600 us pulse, 4080 gap,
- there is no packet gap and 8 repeats
- data is inverted (short=0, long=1) and byte-reflected

S.a. #1824

Temperature is 16 bit, degrees F, scaled x10 +900.
The first reading is the “Meat” channel and the second is for the “Ambient” or grill temperature.
The range would be around -57F to 572F with the manual stating temps higher than 700F could damage the sensor.

- A value of 0x1b58 (7000 / 610F) indicates the sensor is unplugged and sending an E1 error to the displays.
- A value of 0x00c8 (200 / -70F) indicates a sensor problem, which is noted in the manual as E2 error.

The battery status is the MSB of the second byte, 0 for good battery, 1 for low battery signal.

Channel appears random. There are no switches like on other acurite devices and the manual doesn't state anything about channels either.
The channel value seems to be limited to 3, 6, 12 and 15.

Data layout:

    II BC MM MM TT TT XX

- I: 8 bit ID
- B: 4 bit Battery-Low `b???`
- C: 4 bit Random channel, values seen 3, 6, 12, 15
- M: 16 bit Temperature 1 in F x10 +900 (Meat)
- T: 16 bit Temperature 2 in F x10 +900 (Ambient/Grill)
- X: 8 bit Checksum, add with carry

*/

static int acurite_01185m_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int result = 0;
    bitbuffer_invert(bitbuffer);

    // Output the first valid row
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] != 56) {
            result = DECODE_ABORT_LENGTH;
            continue; // return DECODE_ABORT_LENGTH;
        }

        uint8_t *b = bitbuffer->bb[row];
        reflect_bytes(b, 7);
        decoder_log_bitrow(decoder, 2, __func__, b, 7 * 8, "");

        // Verify checksum, add with carry
        int sum = add_bytes(b, 6);
        if ((sum & 0xff) != b[6]) {
            decoder_log_bitrow(decoder, 1, __func__, b, 7 * 8, "bad checksum");
            result = DECODE_FAIL_MIC;
            continue; // return DECODE_FAIL_MIC;
        }
        /* A sanity check to detect some false positives. The following in
           particular checks for a row of 56 "0"s, which would be unreasonable
           temperatures, channel and id of 0, an 'ok' battery, which all
           happens to result in a '0' checksum as well.
        */
        if (sum == 0) {
            return DECODE_FAIL_SANITY;
        }

        // Decode fields
        int id        = (b[0]);
        int batt_low  = (b[1] >> 7);
        int channel   = (b[1] & 0x0f);
        int temp1_raw = (b[2] << 8) | b[3];
        int temp2_raw = (b[4] << 8) | b[5];
        int temp1_ok  = temp1_raw > 200 && temp1_raw < 7000;
        int temp2_ok  = temp2_raw > 200 && temp2_raw < 7000;
        float temp1_f = (temp1_raw - 900) * 0.1f;
        float temp2_f = (temp2_raw - 900) * 0.1f;

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",             DATA_STRING,    "Acurite-01185M",
                "id",               "",             DATA_INT,       id,
                "channel",          "",             DATA_INT,       channel,
                "battery_ok",       "Battery",      DATA_INT,       !batt_low,
                "temperature_1_F",  "Meat",         DATA_COND, temp1_ok, DATA_FORMAT, "%.1f F",   DATA_DOUBLE, temp1_f,
                "temperature_2_F",  "Ambient",      DATA_COND, temp2_ok, DATA_FORMAT, "%.1f F",   DATA_DOUBLE, temp2_f,
                "mic",              "Integrity",    DATA_STRING,    "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    // Only returns the latest result, but better than nothing.
    return result;
}

static char const *const acurite_01185m_output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_1_F",
        "temperature_2_F",
        "mic",
        NULL,
};

r_device const acurite_01185m = {
        .name        = "Acurite Grill/Meat Thermometer 01185M",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 840,  // short pulse is 840 us
        .long_width  = 2070, // long pulse is 2070 us
        .sync_width  = 6600, // sync pulse is 6600 us
        .gap_limit   = 3000, // long gap is 2028 us, sync gap is 4080 us
        .reset_limit = 6000, // no packet gap, sync gap is 4080 us
        .decode_fn   = &acurite_01185m_decode,
        .fields      = acurite_01185m_output_fields,
};
