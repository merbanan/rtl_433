/*
 * FT-004-B Temperature Sensor
 *
 * The sensor sends a packet every 60 seconds. Each frame of 46 bits
 * is sent 3 times without padding/pauses.
 * Format: FFFFFFFF ???????? ???????? tttttttt TTT????? ??????
 *         Fixed type code: 0xf4, Temperature (t=lsb, T=msb), Unknown (?)
 *
 *     {137} 2f cf 24 78 21 c8 bf 3c 91 e0 87 22 fc f2 47 82 1c 80
 *     {137} 2f ce 24 72 a1 70 bf 38 91 ca 85 c2 fc e2 47 2a 17 00
 *
 * Aligning at [..] (insert 2 bits) we get:
 *           2f cf 24 78 21 c8 [..] 2f cf 24 78 21 c8 [..] 2f cf 24 78 21 c8
 *           2f ce 24 72 a1 70 [..] 2f ce 24 72 a1 70 [..] 2f ce 24 72 a1 70
 *
 * Copyright (C) 2017 George Hopkins <george-hopkins@null.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "decoder.h"

static int
ft004b_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t* msg;
    float temperature;
    data_t *data;

    if (bitbuffer->bits_per_row[0] != 137 && bitbuffer->bits_per_row[0] != 138) {
        return 0;
    }

    /* take the majority of all 46 bits (pattern is sent 3 times) and reverse them */
    msg = bitbuffer->bb[0];
    for (int i = 0; i < (46 + 7) / 8; i++) {
        uint8_t a = bitrow_get_byte(msg, i * 8);
        uint8_t b = bitrow_get_byte(msg, i * 8 + 46);
        uint8_t c = bitrow_get_byte(msg, i * 8 + 46 * 2);
        msg[i] = reverse8((a & b) | (b & c) | (a & c));
    }

    if (msg[0] != 0xf4)
        return 0;

    int temp_raw = ((msg[4] & 0x7) << 8) | msg[3];
    temperature = (temp_raw * 0.05f) - 40.0f;

    data = data_make(
            "model", "", DATA_STRING, _X("FT-004B","FT-004-B Temperature Sensor"),
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f", DATA_DOUBLE, temperature,
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "temperature_C",
    NULL
};

r_device ft004b = {
    .name          = "FT-004-B Temperature Sensor",
    .modulation    = OOK_PULSE_PPM,
    .short_width   = 1956,
    .long_width    = 3900,
    .gap_limit     = 4000,
    .reset_limit   = 4000,
    .decode_fn     = &ft004b_callback,
    .disabled      = 0,
    .fields        = output_fields
};
