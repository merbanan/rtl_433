/*
 * *** Fine Offset WH1050 Weather Station ***
 * (aka )
 * (aka .....)
 *
 * This module is a cut-down version of the WH1080 decoder.
 * The WH1050 sensor unit is like the WH1080 unit except it has no
 * wind direction sensor or time receiver.
 * Other than omitting the unused code, the differences are the message length
 * and the location of the battery-low bit.
 *
 * The original module was by
 *
 * 2016 Nicola Quiriti ('ovrheat')
 *
 * Modifications by
 *
 * 2016 Don More
 *
 *********************
 *
 * This weather station is based on an indoor touchscreen receiver, and on a 5+1 outdoor wireless sensors group
 * (rain, wind speed, temperature, humidity.
 * See the product page here: http://www.foshk.com/Weather_Professional/WH1070.html (The 1050 model has no radio clock)
 *
 * Please note that the pressure sensor (barometer) is enclosed in the indoor console unit, NOT in the outdoor
 * wireless sensors group.
 * That's why it's NOT possible to get pressure data by wireless communication. If you need pressure data you should try
 * an Arduino/Raspberry solution wired with a BMP180 or BMP085 sensor.
 *
 * Data are transmitted in a 48 seconds cycle (data packet, then wait 48 seconds, then data packet...).
 *
 * The 'Total rainfall' field is a cumulative counter, increased by 0.3 millimeters of rain at once.
 *
 *
 *
 *
 */


#include "data.h"
#include "rtl_433.h"
#include "util.h"
#include "math.h"

#define CRC_POLY 0x31
#define CRC_INIT 0xff

static unsigned short get_device_id(const uint8_t* br) {
    return (br[1] << 4 & 0xf0 ) | (br[2] >> 4);
}

static char* get_battery(const uint8_t* br) {
    if (!(br[2] & 0x04)) {
        return "OK";
    } else {
        return "LOW";
    }
}

// ------------ WEATHER SENSORS DECODING ----------------------------------------------------

static float get_temperature(const uint8_t* br) {
    const int temp_raw = (br[2] << 8) + br[3];
    return ((temp_raw & 0x03ff) - 0x190) / 10.0;
}

static int get_humidity(const uint8_t* br) {
    return br[4];
}

static float get_wind_speed_raw(const uint8_t* br) {
    return br[5]; // Raw
}

static float get_wind_avg_ms(const uint8_t* br) {
    return (br[5] * 34.0f) / 100; // Meters/sec.
}

static float get_wind_avg_mph(const uint8_t* br) {
    return ((br[5] * 34.0f) / 100) * 2.23693629f; // Mph
}

static float get_wind_avg_kmh(const uint8_t* br) {
    return ((br[5] * 34.0f) / 100) * 3.6f; // Km/h
}

static float get_wind_avg_knot(const uint8_t* br) {
    return ((br[5] * 34.0f) / 100) * 1.94384f; // Knots
}

static float get_wind_gust_raw(const uint8_t* br) {
    return br[6]; // Raw
}

static float get_wind_gust_ms(const uint8_t* br) {
    return (br[6] * 34.0f) / 100; // Meters/sec.
}

static float get_wind_gust_mph(const uint8_t* br) {
    return ((br[6] * 34.0f) / 100) * 2.23693629f; // Mph

}

static float get_wind_gust_kmh(const uint8_t* br) {
    return ((br[6] * 34.0f) / 100) * 3.6f; // Km/h
}

static float get_wind_gust_knot(const uint8_t* br) {
    return ((br[6] * 34.0f) / 100) * 1.94384f; // Knots
}

static float get_rainfall(const uint8_t* br) {
    return ((((unsigned short)br[7] & 0x0f) << 8) | br[8]) * 0.3f;
}


//-------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------



static int fineoffset_wh1050_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);

    if (bitbuffer->num_rows != 1) {
        return 0;
    }
    if (bitbuffer->bits_per_row[0] != 80) {
        return 0;
    }

    const uint8_t *br = bitbuffer->bb[0];

    if (br[0] != 0xff) {
        // preamble missing
        return 0;
    }

    if (br[9] != crc8(br, 9, CRC_POLY, CRC_INIT)) {
        // crc mismatch
        return 0;
    }

//---------------------------------------------------------------------------------------
//-------- GETTING WEATHER SENSORS DATA -------------------------------------------------

    const float temperature = get_temperature(br);
    const int humidity = get_humidity(br);

    // Select which metric system for *wind avg speed* and *wind gust* :

    // Wind average speed :

    //const float speed = get_wind_avg_ms((br)   // <--- Data will be shown in Meters/sec.
    //const float speed = get_wind_avg_mph((br)  // <--- Data will be shown in Mph
    const float speed = get_wind_avg_kmh(br);  // <--- Data will be shown in Km/h
    //const float speed = get_wind_avg_knot((br) // <--- Data will be shown in Knots


    // Wind gust speed :

    //const float gust = get_wind_gust_ms(br);   // <--- Data will be shown in Meters/sec.
    //const float gust = get_wind_gust_mph(br);  // <--- Data will be shown in Mph
    const float gust = get_wind_gust_kmh(br);  // <--- Data will be shown in km/h
    //const float gust = get_wind_gust_knot(br); // <--- Data will be shown in Knots

    const float rain = get_rainfall(br);
    const int device_id = get_device_id(br);
    const char* battery = get_battery(br);

//---------------------------------------------------------------------------------------
//--------- PRESENTING DATA --------------------------------------------------------------

    data = data_make("time",         "",         DATA_STRING, time_str,
        "model",         "",         DATA_STRING, "Fine Offset WH1050 weather station",
        "id",            "StationID",    DATA_FORMAT, "%04X",    DATA_INT,    device_id,
        "temperature_C", "Temperature",    DATA_FORMAT, "%.01f C",    DATA_DOUBLE, temperature,
        "humidity",      "Humidity",    DATA_FORMAT, "%u %%",    DATA_INT,    humidity,
        "speed",         "Wind avg speed",    DATA_FORMAT, "%.02f",    DATA_DOUBLE, speed,
        "gust",          "Wind gust",    DATA_FORMAT, "%.02f",    DATA_DOUBLE, gust,
        "rain",          "Total rainfall",    DATA_FORMAT, "%.01f",    DATA_DOUBLE, rain,
        "battery",       "Battery",    DATA_STRING, battery, // Unsure about Battery byte...
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
    "speed",
    "gust",
    "rain",
    "battery",
    NULL
};

r_device fineoffset_wh1050 = {
    .name           = "Fine Offset WH1050 Weather Station",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 976,
    .long_limit     = 2400,
    .reset_limit    = 10520,
    .json_callback  = &fineoffset_wh1050_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields,
};
