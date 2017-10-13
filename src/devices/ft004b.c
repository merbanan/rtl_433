/*
 * FT-004-B Temperature Sensor
 *
 * The sensor sends a packet every 60 seconds. Each frame of 46 bits
 * is sent 3 times without padding/pauses.
 * Format: ???????? ???????? ??????? TTTTTTTT TTT????? ??????
 *         Temperature (T), Unknown (?)
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

#include "rtl_433.h"
#include "data.h"
#include "util.h"

static float
get_temperature (uint8_t * msg)
{
    uint16_t temp_c = ((msg[4] & 0x7) << 8) | msg[3];
    return (temp_c * 0.05f) - 40.0f;
}

static int
ft004b_callback (bitbuffer_t *bitbuffer)
{
    uint8_t* msg;
    float temperature;
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;

    if(bitbuffer->bits_per_row[0] != 137 && bitbuffer->bits_per_row[0] != 138) {
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

    if (msg[0] == 0xf4) {
        temperature = get_temperature(msg);

        local_time_str(0, time_str);
        data = data_make(
            "time", "", DATA_STRING, time_str,
            "model", "", DATA_STRING, "FT-004-B Temperature Sensor",
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f", DATA_DOUBLE, temperature,
            NULL);
        data_acquired_handler(data);

        return 1;
    }

    return 0;
}

static char *output_fields[] = {
    "time",
    "model",
    "temperature_C",
    NULL
};

r_device ft004b = {
    .name          = "FT-004-B Temperature Sensor",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = (1956 + 3900) / 2,
    .long_limit    = 4000,
    .reset_limit   = 4000,
    .json_callback = &ft004b_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields
};
