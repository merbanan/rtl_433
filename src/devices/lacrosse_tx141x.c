/** @file
    LaCrosse TX141-Bv2, TX141TH-Bv2, TX141-Bv3, TX145wsdth sensor.

    Changes done by Andrew Rivett <veggiefrog@gmail.com>. Copyright is
    retained by Robert Fraczkiewicz.

    Copyright (C) 2017 Robert Fraczkiewicz <aromring@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
LaCrosse TX141-Bv2, TX141TH-Bv2, TX141-Bv3, TX145wsdth sensor.

Also TFA 30.3221.02 (a TX141TH-Bv2),
also TFA 30.3222.02 (a LaCrosse-TX141W).
also TFA 30.3251.10 (a LaCrosse-TX141W).
also some rebrand (ORIA WA50B) with a slightly longer timing, s.a. #2088
also TFA 30.3243.02 (a LaCrosse-TX141Bv3)
also LaCrosse TX141-Bv4 (seems identical to LaCrosse-TX141Bv3)

LaCrosse Color Forecast Station (model C85845), or other LaCrosse product
utilizing the remote temperature/humidity sensor TX141TH-Bv2 transmitting
in the 433.92 MHz band. Product pages:
http://www.lacrossetechnology.com/c85845-color-weather-station/
http://www.lacrossetechnology.com/tx141th-bv2-temperature-humidity-sensor

The TX141TH-Bv2 protocol is OOK modulated PWM with fixed period of 625 us
for data bits, preambled by four long startbit pulses of fixed period equal
to ~1666 us. Hence, it is similar to Bresser Thermo-/Hygro-Sensor 3CH.

A single data packet looks as follows:
1) preamble - 833 us high followed by 833 us low, repeated 4 times:

     ----      ----      ----      ----
    |    |    |    |    |    |    |    |
          ----      ----      ----      ----

2) a train of 40 data pulses with fixed 625 us period follows immediately:

     ---    --     --     ---    ---    --     ---
    |   |  |  |   |  |   |   |  |   |  |  |   |   |
         --    ---    ---     --     --    ---     -- ....

A logical 1 is 417 us of high followed by 208 us of low.
A logical 0 is 208 us of high followed by 417 us of low.
Thus, in the example pictured above the bits are 1 0 0 1 1 0 1 ....

The TX141TH-Bv2 sensor sends 12 of identical packets, one immediately following
the other, in a single burst. These 12-packet bursts repeat every 50 seconds. At
the end of the last packet there are two 833 us pulses ("post-amble"?).

The TX141-Bv3 has a revision which only sends 4 packets per transmission.

The data is grouped in 5 bytes / 10 nybbles

    [id] [id] [flags] [temp] [temp] [temp] [humi] [humi] [chk] [chk]

- id:    8 bit random integer generated at each powers up
- flags: 4 bit for battery low indicator, test button press, and channel
- temp: 12 bit unsigned temperature in degrees Celsius, scaled by 10, offset 500, range -40 C to 60 C
- humi:  8 bit integer indicating relative humidity in %.
- chk:   8 bit checksum is a digest, 0x31, 0xf4, reflected

A count enables us to determine the quality of radio transmission.

*** Addition of TX141 temperature only device, Jan 2018 by Andrew Rivett <veggiefrog@gmail.com>**

The TX141-BV2 is the temperature only version of the TX141TH-BV2 sensor.

Changes:
- Changed minimum bit length to 32 (tx141b is temperature only)
- LACROSSE_TX141_BITLEN is 37 instead of 40.
- The humidity variable has been removed for TX141.
- Battery check bit is inverse of TX141TH.
- temp_f removed, temp_c (celsius) is what's provided by the device.

- TX141TH-BV3 bitlen is 41

Addition of TX141W and TX145wsdth:

    PRE5b ID19h BAT1b TEST?1b CH?2h TYPE4h TEMP_WIND12d HUM_DIR12d CHK8h 1x

- type 1 has temp+hum (temp is offset 500 and scale 10)
- type 2 has wind speed (km/h scale 10) and direction (degrees)
- checksum is CRC-8 poly 0x31 init 0x00 over preceding 7 bytes

*/

#include "decoder.h"

// Define the types of devices this file supports (uses expected bitlen)
#define LACROSSE_TX141B 32
#define LACROSSE_TX141 37
#define LACROSSE_TX141TH 40
#define LACROSSE_TX141BV3 33
#define LACROSSE_TX141W 65

static int lacrosse_tx141x_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int r;
    int device;
    uint8_t *b;

    // Find the most frequent data packet
    // reduce false positives, require at least 5 out of 12 or 3 of 4 repeats.
    // allows 4-repeats transmission to contain a bogus extra row.
    r = bitbuffer_find_repeated_row(bitbuffer, bitbuffer->num_rows > 5 ? 5 : 3, 32); // 32
    if (r < 0) {
        // try again for TX141W/TX145wsdth, require at least 2 out of 3-7 repeats.
        r = bitbuffer_find_repeated_row(bitbuffer, 2, 64); // 65
    }
    if (r < 0) {
        return DECODE_ABORT_LENGTH;
    }

    if (bitbuffer->bits_per_row[r] >= 64) {
        device = LACROSSE_TX141W;
    }
    else if (bitbuffer->bits_per_row[r] > 41) {
        return DECODE_ABORT_LENGTH;
    }
    else if (bitbuffer->bits_per_row[r] >= 41) {
        if (bitbuffer->num_rows > 12) {
            return DECODE_ABORT_LENGTH; // false-positive with GT-WT03
        }
        device = LACROSSE_TX141TH; // actually TX141TH-BV3
    }
    else if (bitbuffer->bits_per_row[r] >= 40) {
        device = LACROSSE_TX141TH;
    }
    else if (bitbuffer->bits_per_row[r] >= 37) {
        device = LACROSSE_TX141;
    }
    else if (bitbuffer->bits_per_row[r] == 32) {
        device = LACROSSE_TX141B;
    } else {
        device = LACROSSE_TX141BV3;
    }

    bitbuffer_invert(bitbuffer);
    b = bitbuffer->bb[r];

    if (device == LACROSSE_TX141W) {
        int pre = (b[0] >> 3);
        if (pre != 0x01) {
            return DECODE_ABORT_EARLY;
        }

        int chk = crc8(b, 8, 0x31, 0x00);
        if (chk) {
            return DECODE_FAIL_MIC;
        }

        int id          = ((b[0] & 0x07) << 16) | (b[1] << 8) | b[2];
        int battery_low = (b[3] >> 7);
        int test        = (b[3] & 0x40) >> 6;
        int channel     = (b[3] & 0x30) >> 4;
        int type        = (b[3] & 0x0f);
        int temp_raw    = (b[4] << 4) | (b[5] >> 4);
        int humidity    = ((b[5] & 0x0f) << 8) | b[6];

        if (type == 1) {
            // Temp/Hum
            float temp_c = (temp_raw - 500) * 0.1f;

            /* clang-format off */
            data = data_make(
                    "model",            "",                 DATA_STRING, "LaCrosse-TX141W",
                    "id",               "Sensor ID",        DATA_FORMAT, "%05x", DATA_INT, id,
                    "channel",          "Channel",          DATA_FORMAT, "%01x", DATA_INT, channel,
                    "battery_ok",       "Battery level",    DATA_INT,    !battery_low,
                    "temperature_C",    "Temperature",      DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
                    "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    "test",             "Test?",            DATA_INT,    test,
                    "mic",              "Integrity",        DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
        }
        else if (type == 2) {
            // Wind
            float speed_kmh = temp_raw * 0.1f;
            // wind direction is in humidity field

            /* clang-format off */
            data = data_make(
                    "model",            "",                 DATA_STRING, "LaCrosse-TX141W",
                    "id",               "Sensor ID",        DATA_FORMAT, "%05x", DATA_INT, id,
                    "channel",          "Channel",          DATA_FORMAT, "%01x", DATA_INT, channel,
                    "battery_ok",       "Battery level",    DATA_INT,    !battery_low,
                    "wind_avg_km_h",    "Wind speed",       DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, speed_kmh,
                    "wind_dir_deg",     "Wind direction",   DATA_INT,    humidity,
                    "test",             "Test?",            DATA_INT,    test,
                    "mic",              "Integrity",        DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */
        }
        else {
            decoder_logf(decoder, 1, __func__, "unknown subtype: %d", type);
            return DECODE_FAIL_OTHER;
        }

        decoder_output_data(decoder, data);
        return 1;
    }

    int id = b[0];
    int battery_low;
    if (device == LACROSSE_TX141TH) {
        battery_low = (b[1] >> 7);
    }
    else { // LACROSSE_TX141 || LACROSSE_TX141BV3
        battery_low = !(b[1] >> 7);
    }
    int test     = (b[1] & 0x40) >> 6;
    int channel  = (b[1] & 0x30) >> 4;
    int temp_raw = ((b[1] & 0x0F) << 8) | b[2];
    float temp_c = (temp_raw - 500) * 0.1f; // Temperature in C

    int humidity = 0;
    if (device == LACROSSE_TX141TH) {
        humidity = b[3];
    }

    if (0 == id || (device == LACROSSE_TX141TH && (0 == humidity || humidity > 100)) || temp_c < -40.0 || temp_c > 140.0) {
        decoder_logf(decoder, 1, __func__, "data error, id: %i, humidity:%i, temp:%f", id, humidity, temp_c);
        return DECODE_FAIL_SANITY;
    }

    if (device == LACROSSE_TX141B) {
        /* clang-format off */
        data = data_make(
                "model",         "",              DATA_STRING, "LaCrosse-TX141B",
                "id",            "Sensor ID",     DATA_FORMAT, "%02x", DATA_INT, id,
                "temperature_C", "Temperature",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
                "battery_ok",    "Battery",       DATA_INT,    !battery_low,
                "test",          "Test?",         DATA_STRING, test ? "Yes" : "No",
                NULL);
        /* clang-format on */
    } else if (device == LACROSSE_TX141) {
        /* clang-format off */
        data = data_make(
                "model",         "",              DATA_STRING, "LaCrosse-TX141Bv2",
                "id",            "Sensor ID",     DATA_FORMAT, "%02x", DATA_INT, id,
                "channel",       "Channel",       DATA_INT, channel,
                "temperature_C", "Temperature",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
                "battery_ok",    "Battery",       DATA_INT,    !battery_low,
                "test",          "Test?",         DATA_STRING, test ? "Yes" : "No",
                NULL);
        /* clang-format on */
    }
    else if (device == LACROSSE_TX141BV3) {
        /* clang-format off */
        data = data_make(
                "model",         "",              DATA_STRING, "LaCrosse-TX141Bv3",
                "id",            "Sensor ID",     DATA_FORMAT, "%02x", DATA_INT, id,
                "channel",       "Channel",       DATA_INT, channel,
                "battery_ok",    "Battery",       DATA_INT,    !battery_low,
                "temperature_C", "Temperature",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
                "test",          "Test?",         DATA_STRING, test ? "Yes" : "No",
                NULL);
        /* clang-format on */
    }
    else {
        // Digest check for TX141TH-Bv2
        if (lfsr_digest8_reflect(b, 4, 0x31, 0xf4) != b[4]) {
            decoder_logf(decoder, 1, __func__, "Checksum digest TX141TH failed");
            return DECODE_FAIL_MIC;
        }
        /* clang-format off */
        data = data_make(
                "model",         "",              DATA_STRING, "LaCrosse-TX141THBv2",
                "id",            "Sensor ID",     DATA_FORMAT, "%02x", DATA_INT, id,
                "channel",       "Channel",       DATA_INT, channel,
                "battery_ok",    "Battery",       DATA_INT,    !battery_low,
                "temperature_C", "Temperature",   DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
                "humidity",      "Humidity",      DATA_FORMAT, "%u %%", DATA_INT, humidity,
                "test",          "Test?",         DATA_STRING, test ? "Yes" : "No",
                "mic",           "Integrity",     DATA_STRING, "CRC",
                NULL);
        /* clang-format on */
    }

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_avg_km_h",
        "wind_dir_deg",
        "test",
        "mic",
        NULL,
};

// note TX141W, TX145wsdth: m=OOK_PWM, s=256, l=500, r=1888, y=748
r_device const lacrosse_tx141x = {
        .name        = "LaCrosse TX141-Bv2, TX141TH-Bv2, TX141-Bv3, TX141W, TX145wsdth, (TFA, ORIA) sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 208,  // short pulse is 208 us + 417 us gap
        .long_width  = 417,  // long pulse is 417 us + 208 us gap
        .sync_width  = 833,  // sync pulse is 833 us + 833 us gap
        .gap_limit   = 625,  // long gap (with short pulse) is ~417 us, sync gap is ~833 us
        .reset_limit = 1700, // maximum gap is 1250 us (long gap + longer sync gap on last repeat)
        .decode_fn   = &lacrosse_tx141x_decode,
        .fields      = output_fields,
};
