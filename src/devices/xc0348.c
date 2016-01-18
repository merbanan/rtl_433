/*
 * Digitech XC0348 weather station
 * Reports 1 row, 88 pulses
 * Format: ff ID ?X XX YY ZZ ?? ?? ?? UU CC
 * - ID: device id
 * - ?X XX: temperature, likely in 0.1C steps (.1 e7 == 8.7C, .1 ef == 9.5C)
 * - YY: percent in a single byte (for example 54 == 84%)
 * - ZZ: wind speed (00 == 0, 01 == 1.1km/s, ...)
 * - UU: wind direction: 00 is N, 02 is NE, 04 is E, etc. up to 0F is seems
 * - CC: checksum
 *
 * still unknown - rain, pressure
 */

#include "rtl_433.h"
#include "util.h"

#define CRC_POLY 0x31
#define CRC_INIT 0xff

static const char* wind_directions[] = {
    "N", "NNE", "NE",
    "ENE", "E", "ESE",
    "SE", "SSE", "S", "SSW", "SW",
    "WSW", "W", "WNW",
    "NW", "NNW",
};

static float get_temperature(const uint8_t* br) {
    const int temp_raw = (br[2] << 8) + br[3];
    return ((temp_raw & 0x0fff) - 0x190) / 10.0;
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

    if (br[0] != 0xff) {
        // preamble missing
        return 0;
    }

    if (br[10] != crc8(br, 10, CRC_POLY, CRC_INIT)) {
        // crc mismatch
        return 0;
    }

    const float temperature = get_temperature(br);
    const int humidity = get_humidity(br);
    const char* direction = get_wind_direction(br);
    const float speed = get_wind_speed(br);

    fprintf(stdout, "Temperature event:\n");
    fprintf(stdout, "protocol      = Digitech XC0348 weather station\n");
    fprintf(stdout, "device        = %02x\n", br[1]);
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
    .long_limit     = 600,
    .reset_limit    = 2630,
    .json_callback  = &digitech_ws_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};
