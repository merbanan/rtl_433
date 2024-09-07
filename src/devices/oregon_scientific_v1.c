/** @file
    OSv1 protocol.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
OSv1 protocol.

MC with nominal bit width of 2930 us.
Pulses are somewhat longer than nominal half-bit width, 1748 us / 3216 us,
Gaps are somewhat shorter than nominal half-bit width, 1176 us / 2640 us.
After 12 preamble bits there is 4200 us gap, 5780 us pulse, 5200 us gap.

Care must be taken with the gap after the sync pulse since it
is outside of the normal clocking.  Because of this a data stream
beginning with a 0 will have data in this gap.

*/

#include "decoder.h"

#define OSV1_BITS   32

static int oregon_scientific_v1_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = 0;
    int nibble[OSV1_BITS/4];

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] != OSV1_BITS)
            continue; // DECODE_ABORT_LENGTH

        int cs = 0;
        for (int i = 0; i < OSV1_BITS / 8; i++) {
            uint8_t byte = reverse8(bitbuffer->bb[row][i]);
            nibble[i * 2    ] = byte & 0x0f;
            nibble[i * 2 + 1] = byte >> 4;
            if (i < ((OSV1_BITS / 8) - 1))
                cs += nibble[i * 2] + 16 * nibble[i * 2 + 1];
        }


        // No need to decode/extract values for simple test
        if (bitbuffer->bb[row][0] == 0xFF && bitbuffer->bb[row][1] == 0xFF
                && bitbuffer->bb[row][2] == 0xFF && bitbuffer->bb[row][3] == 0xFF )  {
            decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0xff");
            continue; //  DECODE_FAIL_SANITY
        }

        cs = (cs & 0xFF) + (cs >> 8);
        int checksum = nibble[6] + (nibble[7] << 4);
        /* reject 0x00 checksums to reduce false positives */
        if (!checksum || (checksum != cs))
            continue; // DECODE_FAIL_MIC

        int sid      = nibble[0];
        int channel  = ((nibble[1] >> 2) & 0x03) + 1;
        //int uk1      = (nibble[1] >> 0) & 0x03; /* unknown.  Seen change every 60 minutes */
        float temp_c =  nibble[2] * 0.1f + nibble[3] + nibble[4] * 10.0f;
        int battery  = (nibble[5] >> 3) & 0x01;
        //int uk2      = (nibble[5] >> 2) & 0x01; /* unknown.  Always zero? */
        int sign     = (nibble[5] >> 1) & 0x01;
        //int uk3      = (nibble[5] >> 0) & 0x01; /* unknown.  Always zero? */

        if (sign)
            temp_c = -temp_c;

        /* clang-format off */
        data_t *data = data_make(
                "model",            "",             DATA_STRING,    "Oregon-v1",
                "id",               "SID",          DATA_INT,       sid,
                "channel",          "Channel",      DATA_INT,       channel,
                "battery_ok",       "Battery",      DATA_INT,       !battery,
                "temperature_C",    "Temperature",  DATA_FORMAT,    "%.1f C",              DATA_DOUBLE,    temp_c,
                "mic",              "Integrity",    DATA_STRING,    "CHECKSUM",
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
        "temperature_C",
        "mic",
        NULL,
};

r_device const oregon_scientific_v1 = {
        .name        = "OSv1 Temperature Sensor",
        .modulation  = OOK_PULSE_PWM_OSV1,
        .short_width = 1465, // nominal half-bit width
        .sync_width  = 5780,
        .gap_limit   = 3500,
        .reset_limit = 14000,
        .decode_fn   = &oregon_scientific_v1_callback,
        .fields      = output_fields,
};
