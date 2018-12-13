/*
 * *** Fine Offset WH1050 Weather Station ***
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
 */

#include "decoder.h"

#define CRC_POLY 0x31

// If you calculate the CRC over all 10 bytes including the preamble
// byte (always 0xFF), then CRC_INIT is 0xFF. But we compare the preamble
// byte and then discard it.
#define CRC_INIT 0x00

static unsigned short get_device_id(const uint8_t* br) {
    return (br[0] << 4 & 0xf0 ) | (br[1] >> 4);
}

static char* get_battery(const uint8_t* br) {
    if (!(br[1] & 0x04)) {
        return "OK";
    } else {
        return "LOW";
    }
}

// ------------ WEATHER SENSORS DECODING ----------------------------------------------------

static float get_temperature(const uint8_t* br) {
    const int temp_raw = (br[1] << 8) + br[2];
    return ((temp_raw & 0x03ff) - 0x190) / 10.0;
}

static int get_humidity(const uint8_t* br) {
    return br[3];
}

static float get_wind_speed_raw(const uint8_t* br) {
    return br[4]; // Raw
}

static float get_wind_avg_ms(const uint8_t* br) {
    return (br[4] * 34.0f) / 100; // Meters/sec.
}

static float get_wind_avg_mph(const uint8_t* br) {
    return ((br[4] * 34.0f) / 100) * 2.23693629f; // Mph
}

static float get_wind_avg_kmh(const uint8_t* br) {
    return ((br[4] * 34.0f) / 100) * 3.6f; // Km/h
}

static float get_wind_avg_knot(const uint8_t* br) {
    return ((br[4] * 34.0f) / 100) * 1.94384f; // Knots
}

static float get_wind_gust_raw(const uint8_t* br) {
    return br[5]; // Raw
}

static float get_wind_gust_ms(const uint8_t* br) {
    return (br[5] * 34.0f) / 100; // Meters/sec.
}

static float get_wind_gust_mph(const uint8_t* br) {
    return ((br[5] * 34.0f) / 100) * 2.23693629f; // Mph

}

static float get_wind_gust_kmh(const uint8_t* br) {
    return ((br[5] * 34.0f) / 100) * 3.6f; // Km/h
}

static float get_wind_gust_knot(const uint8_t* br) {
    return ((br[5] * 34.0f) / 100) * 1.94384f; // Knots
}

static float get_rainfall(const uint8_t* br) {
    return ((((unsigned short)br[6] & 0x0f) << 8) | br[7]) * 0.3f;
}

//-------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------

static int fineoffset_wh1050_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t br[9];

    if (bitbuffer->num_rows != 1) {
        return 0;
    }

    /* The normal preamble for WH1050 is 8 1s (0xFF) followed by 4 0s
       for a total 80 bit message.
       (The 4 0s is not confirmed to be preamble but seems to be zero on most devices)

       Digitech XC0346 (and possibly other models) only sends 7 1 bits not 8 (0xFE)
       for some reason (maybe transmitter module is slow to wake up), for a total
       79 bit message.

       In both cases, we extract the 72 bits after the preamble.
    */
    unsigned bits = bitbuffer->bits_per_row[0];
    uint8_t preamble_byte = bitbuffer->bb[0][0];
    if (bits == 79 && preamble_byte == 0xfe) {
        bitbuffer_extract_bytes(bitbuffer, 0, 7, br, 72);
    } else if (bits == 80 && preamble_byte == 0xff) {
        bitbuffer_extract_bytes(bitbuffer, 0, 8, br, 72);
    } else {
        return 0;
    }

    if (br[8] != crc8(br, 8, CRC_POLY, CRC_INIT)) {
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

    data = data_make(
            "model",         "",         DATA_STRING, "Fine Offset WH1050 weather station",
            "id",            "StationID",    DATA_FORMAT, "%04X",    DATA_INT,    device_id,
            "temperature_C", "Temperature",    DATA_FORMAT, "%.01f C",    DATA_DOUBLE, temperature,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%",    DATA_INT,    humidity,
            "speed",         "Wind avg speed",    DATA_FORMAT, "%.02f",    DATA_DOUBLE, speed,
            "gust",          "Wind gust",    DATA_FORMAT, "%.02f",    DATA_DOUBLE, gust,
            "rain",          "Total rainfall",    DATA_FORMAT, "%.01f",    DATA_DOUBLE, rain,
            "battery",       "Battery",    DATA_STRING, battery, // Unsure about Battery byte...
            "mic",             "Integrity",    DATA_STRING,    "CRC",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
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
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 544,
    .long_width     = 1524,
    .reset_limit    = 10520,
    .decode_fn      = &fineoffset_wh1050_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
