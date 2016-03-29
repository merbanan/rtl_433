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

#include "data.h"
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
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);

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
    const char device_id = br[1];

    data = data_make("time",          "",               DATA_STRING, time_str,
                     "model",         "",               DATA_STRING, "Digitech XC0348 weather station",
                     "id",            "",               DATA_INT,    device_id,
                     "temperature_C", "Temperature",    DATA_DOUBLE, temperature,
                     "humidity",      "Humidity",       DATA_INT,    humidity,
                     "direction",     "Wind direction", DATA_STRING, direction,
                     "speed",         "Wind speed",     DATA_DOUBLE, speed,
                     NULL);
    data_acquired_handler(data);
    return 1;
}

static char *output_fields[] = {
	"time",
	"model",
	"id",
	"temperature_C",
	"humidity",
	"direction",
	"speed",
	NULL
};

r_device digitech_ws = {
    .name           = "Digitech XC0348 Weather Station",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 976,
    .long_limit     = 2400,
    .reset_limit    = 10520,
    .json_callback  = &digitech_ws_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields,
};
