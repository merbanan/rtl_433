/*
 * Digitech XC0348 weather station
 * Reports 1 row, 88 pulses
 * Format: ff a8 XX XX YY ZZ 01 04 5d UU CC
 * - XX XX: temperature, likely in 0.1C steps (51 e7 == 8.7C, 51 ef == 9.5C)
 * - YY: percent in a single byte (for example 54 == 84%)
 * - ZZ: wind speed (00 == 0, 01 == 1.1km/s, ...)
 * - UU: wind direction: 00 is N, 02 is NE, 04 is E, etc. up to 0F is seems
 * - CC: unknown checksum (or less likely - part of wind direction)
 *
 * still unknown - rain, pressure
 */

#include "rtl_433.h"

static const char* wind_directions[] = {
    "N", "NNE", "NE",
    "ENE", "E", "ESE",
    "SE", "SSE", "S", "SSW", "SW",
    "WSW", "W", "WNW",
    "NW", "NNW",
};

static float get_temperature(const uint8_t* br) {
    const int temp_raw = (br[2] << 8) + br[3];
    return (temp_raw - 0x5190) / 10.0;
}

static int get_humidity(const uint8_t* br) {
    return br[4];
}

static const char* get_wind_direction(const uint8_t* br) {
    return wind_directions[br[9] & 0x0f];
}

static float get_wind_speed(const uint8_t* br) {
    return br[5] * 1.1f;
}

static int digitech_ws_callback(bitbuffer_t *bitbuffer) {
    if (bitbuffer->num_rows != 1) {
        return 0;
    }
    if (bitbuffer->bits_per_row[0] != 88) {
        return 0;
    }

	const uint8_t *br = bitbuffer->bb[0];

    if (br[0] != 0xff || br[1] != 0xa8) {
        fprintf(stdout, "digitech header mismatch: 0x%02x 0x%02x\n", br[0], br[1]);
        return 0;
    }

    /* TODO: checksum validation */

    const float temperature = get_temperature(br);
    const int humidity = get_humidity(br);
    const char* direction = get_wind_direction(br);
    const float speed = get_wind_speed(br);

    fprintf(stdout, "Temperature event:\n");
    fprintf(stdout, "protocol      = Digitech XC0348 weather station\n");
    fprintf(stdout, "temp          = %.1f°C\n", temperature);
    fprintf(stdout, "humidity      = %d%%\n", humidity);
    fprintf(stdout, "direction     = %s\n", direction);
    fprintf(stdout, "speed         = %.1f km/h\n", speed);
    fprintf(stdout, "unknown       = %02x %02x %02x\n\n", br[6], br[7], br[8]);

    return 1;
}

r_device digitech_ws = {
    .name           = "Digitech XC0348 Weather Station",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 244,
    .long_limit     = 266,
    .reset_limit    = 263,
    .json_callback  = &digitech_ws_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};
