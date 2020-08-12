/** @file
    Opus/Imagintronix XT300 Soil Moisture Sensor.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Opus/Imagintronix XT300 Soil Moisture Sensor.

Also called XH300 sometimes, this seems to be the associated display name

https://www.plantcaretools.com/product/wireless-moisture-monitor/

Data is transmitted with 6 bytes row:

  0. 1. 2. 3. 4. 5
 FF ID SM TT ?? CC

FF: initial preamble
ID: 0101 01ID
SM: soil moisure (decimal 05 -> 99 %)
TT: temperature °C + 40°C (decimal)
??: always FF... maybe spare bytes
CC: check sum (simple sum) except 0xFF preamble
 
*/

#include "decoder.h"

static int opus_xt300_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = 0;
    int fail_code = 0;
    int row;
    int chk;
    uint8_t *b;
    int channel, temp, moisture;
    data_t *data;

    for (row = 0; row < bitbuffer->num_rows; row++) {

        if (bitbuffer->bits_per_row[row] != 48) {
            fail_code = DECODE_ABORT_LENGTH;
            continue;
        }

        b = bitbuffer->bb[row];

        if (!b[0] && !b[1] && !b[2] && !b[3]) {
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s: DECODE_FAIL_SANITY data all 0x00\n", __func__);
            }
            fail_code = DECODE_FAIL_SANITY;
            continue;
        }

        if (b[0] != 0xFF && ((b[1] | 0x1) & 0xFD) == 0x55) {
            fail_code = DECODE_ABORT_EARLY;
            continue;
        }
        chk = add_bytes(b + 1, 4); // sum bytes 1-4
        chk = chk & 0xFF;
        if (chk != 0 && chk != b[5] ) {
            fail_code =  DECODE_FAIL_MIC;
            continue;
        }

        channel  = (b[1] & 0x03);
        temp     = b[3] - 40;
        moisture =  b[2];

        // unverified sales advert say Outdoor temperature range: -40°C to +65°C
        // test for Boiling water
        // over 100% soil humidity ?
        if (temp > 100 || moisture > 101) {
            // fprintf(stderr, "%s: temp %d moisture %d\n", __func__, temp, moisture);
            fail_code = DECODE_FAIL_SANITY;
            continue;
        }

        data = data_make(
            "model",            "",             DATA_STRING, "Opus-XT300",
            "channel",          "Channel",      DATA_INT,    channel,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%d C", DATA_INT, temp,
            "moisture",         "Moisture",     DATA_FORMAT, "%d %%", DATA_INT, moisture,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
        decoder_output_data(decoder, data);
        ret++;
    }
    return ret > 0 ? ret : fail_code;
}

static char *output_fields[] = {
    "model",
    "channel",
    "temperature_C",
    "moisture",
    NULL
};


r_device opus_xt300 = {
    .name           = "Opus/Imagintronix XT300 Soil Moisture",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 544,
    .long_width     = 932,
    .gap_limit      = 10000,
    .reset_limit    = 31000,
    .decode_fn      = &opus_xt300_callback,
    .disabled       = 0,
    .fields         = output_fields
};
