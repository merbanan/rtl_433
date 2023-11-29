/** @file
    LaCrosse TX31U-IT protocol.

    Copyright (C) 2023 Craig Johnston

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Decoder for LaCrosse transmitter provided with the WS-1910TWC-IT product.
Branded with "The Weather Channel" logo.
https://www.lacrossetechnology.com/products/ws-1910twc-it

FCC ID: OMO-TX22U
FSK_PCM @915 MHz, 116usec/bit

## Protocol

Data format:

This transmitter uses a variable length protocol that includes 1-5 measurements
of 2 bytes each.  The first nibble of each measurement identifies the sensor.

    Sensor      Code    Encoding
    TEMP          0       BCD tenths of a degree C plus 400 offset.
                              EX: 0x0653 is 25.3 degrees C
    HUMID         1       BCD % relative humidity.
                              EX: 0x1068 is 68%
    UNKNOWN       2       This is probably reserved for a rain gauge (TX32U-IT) - NOT TESTED
    WIND_AVG_DIR  3       Wind direction and decimal time averaged wind speed in m/sec.
                              First nibble is direction in units of 22.5 degrees.
    WIND_MAX      4       Decimal maximum wind speed in m/sec during last reporting interval.
                              First nibble is 0x1 if wind sensor input is lost.


       a    a    a    a    2    d    d    4    a    2    e    5    0    6    5    3    c    0
    Bits :
    1010 1010 1010 1010 0010 1101 1101 0100 1010 0010 1110 0101 0000 0110 0101 0011 1100 0000
    Bytes num :
    ----1---- ----2---- ----3---- ----4---- ----5---- ----6---- ----7---- ----8---- ----N----
    ~~~~~~~~~~~~~~~~~~~ 2 bytes preamble (0xaaaa)
                        ~~~~~~~~~~~~~~~~~~~ bytes 3 and 4 sync word of 0x2dd4
    sensor model (always 0xa)               ~~~~ 1st nibble of byte 5
    Random device id (6 bits)                    ~~~~ ~~ 2nd nibble of byte 5 and bits 7-6 of byte 6
    Initial training mode (all sensors report)          ~ bit 5 of byte 6
    no external sensor detected                          ~ bit 4 of byte 6
    low battery indication                                 ~ bit 3 of byte 6
    count of sensors reporting (1 to 5)                     ~~~ bits 2,1,0 of byte 6
    sensor code                                                 ~~~~ 1st nibble of byte 7
    sensor reading (meaning varies, see above)                       ~~~~ ~~~~ ~~~~ 2nd nibble of byte 7 and byte 8
    ---
    --- repeat sensor code:reading as specified in count value above
    ---
    crc8 (poly 0x31 init 0x00) of bytes 5 thru (N-1)                                ~~~~ ~~~~ last byte

## Developer's comments

The WS-1910TWC-IT does not have a rain gauge or wind direction vane.  The readings output here
are inferred from the output data, and correlating it with other similar Lacrosse devices.
These readings have not been tested.

*/

#include "decoder.h"

#define BIT(pos)               (1 << (pos))
#define CHECK_BIT(y, pos)      ((0u == ((y) & (BIT(pos)))) ? 0u : 1u)
#define SET_LSBITS(len)        (BIT(len) - 1)                       // the first len bits are '1' and the rest are '0'
#define BF_PREP(y, start, len) (((y) & SET_LSBITS(len)) << (start)) // Prepare a bitmask
#define BF_GET(y, start, len)  (((y) >> (start)) & SET_LSBITS(len))

#define TX31U_MIN_LEN_BYTES    9  // assume at least one measurement
#define TX31U_MAX_LEN_BYTES    20 // actually shouldn't be more than 18, but we'll be generous

static int lacrosse_tx31u_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    // There will only be one row
    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }

    // search for expected start sequence
    uint8_t const start_match[] = {0xaa, 0xaa, 0x2d, 0xd4}; // preamble + sync word (32 bits)
    unsigned int start_pos      = bitbuffer_search(bitbuffer, 0, 0, start_match, sizeof(start_match) * 8);
    if (start_pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }
    uint8_t msg_bytes = (bitbuffer->bits_per_row[0] - start_pos) / 8;

    if (msg_bytes < TX31U_MIN_LEN_BYTES) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bytes", msg_bytes);
        return DECODE_ABORT_LENGTH;
    }
    else if (msg_bytes > TX31U_MAX_LEN_BYTES) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bytes", msg_bytes);
        return DECODE_ABORT_LENGTH;
    }
    else {
        decoder_logf(decoder, 2, __func__, "packet length: %d", msg_bytes);
    }

    decoder_log(decoder, 1, __func__, "LaCrosse TX31U-IT detected");

    uint8_t msg[TX31U_MAX_LEN_BYTES];
    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, msg_bytes * 8);

    // int model = BF_GET(msg[4], 4, 4);
    int sensor_id = (BF_GET(msg[4], 0, 4) << 2) | BF_GET(msg[5], 6, 2);
    // int training = CHECK_BIT(msg[5], 5);
    int no_ext_sensor = CHECK_BIT(msg[5], 4);
    int battery_low   = CHECK_BIT(msg[5], 3);
    int measurements  = BF_GET(msg[5], 0, 3);

    // Check message integrity
    int expected_bytes = 6 + measurements * 2 + 1;
    if (msg_bytes >= expected_bytes) { // did we get shorted?
        int r_crc = msg[expected_bytes - 1];
        int c_crc = crc8(&msg[4], 2 + measurements * 2, 0x31, 0x00);
        if (r_crc != c_crc) {
            decoder_logf(decoder, 1, __func__, "LaCrosse TX31U-IT bad CRC: calculated %02x, received %02x", c_crc, r_crc);
            return DECODE_FAIL_MIC;
        }
    }
    else {
        decoder_logf(decoder, 1, __func__, "Packet truncated: received %d bytes, expected %d bytes", msg_bytes, expected_bytes);
        return DECODE_ABORT_LENGTH;
    }

    /* clang-format off */
    // what we know from the header
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "LaCrosse-TX31UIT",
            "id",               "",             DATA_INT,    sensor_id,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            NULL);

    // decode each measurement we get and append them.
    enum sensor_type { TEMP=0, HUMIDITY, RAIN, WIND_AVG, WIND_MAX };
    for (int m=0; m<measurements; ++m ) {
        uint8_t type = BF_GET(msg[6+m*2], 4, 4 );
        uint8_t nib1 = BF_GET(msg[6+m*2], 0, 4 );
        uint8_t nib2 = BF_GET(msg[7+m*2], 4, 4 );
        uint8_t nib3 = BF_GET(msg[7+m*2], 0, 4 );
        switch (type) {
            case TEMP: {
                float temp_c = 10*nib1 + nib2 + 0.1f*nib3 - 40.0f; // BCD offset 40 deg C
                data = data_append( data,
                    "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    NULL);
            } break;
            case HUMIDITY: {
                int humidity = 100*nib1 + 10*nib2 + nib3; // BCD %
                data = data_append( data,
                    "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    NULL);
            } break;
            case RAIN: {
                int raw_rain = (nib1<<8) + (nib2<<4) + nib3; // count of contact closures
                if ( !no_ext_sensor && raw_rain > 0) { // most of these do not have rain gauges.  Suppress output if zero.
                    data = data_append( data,
                        "rain",         "raw_rain",     DATA_FORMAT, "%03x", DATA_INT, raw_rain,
                        NULL);
                }
            } break;
            case WIND_AVG: {
                if ( !no_ext_sensor ) {
                    float wind_dir = nib1 * 22.5 ; // compass direction in degrees
                    float wind_avg = ((nib2<<4) + nib3) * 0.1f * 3.6; // wind values are decimal m/sec, convert to km/hr
                    data = data_append( data,
                        "wind_dir_deg",   "Wind direction",  DATA_FORMAT, "%.1f",       DATA_DOUBLE, wind_dir,
                        "wind_avg_km_h",  "Wind speed",      DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, wind_avg,
                        NULL);
                }
            } break;
            case WIND_MAX: {
                int wind_input_lost = CHECK_BIT(nib1, 0); // a sensor was attached, but now not detected
                if ( !no_ext_sensor && !wind_input_lost ) {
                    float wind_max = ((nib2<<4) + nib3) * 0.1f * 3.6; // wind values are decimal m/sec, convert to km/hr
                    data = data_append( data,
                        "wind_max_km_h",  "Wind gust",    DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, wind_max,
                        NULL);
                }
            } break;
            default:
                decoder_logf(decoder, 1, __func__, "LaCrosse TX31U-IT unknown sensor type %d", type);
            break;
        }
    }

    data = data_append( data,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "humidity",
        "wind_avg_km_h",
        "wind_max_km_h",
        "wind_dir_deg",
        "mic",
        NULL,
};

// Receiver for the Lacrosse TX31U-IT
r_device const lacrosse_tx31u = {
        .name        = "LaCrosse TX31U-IT, The Weather Channel WS-1910TWC-IT",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 116,
        .long_width  = 116,
        .reset_limit = 20000,
        .decode_fn   = &lacrosse_tx31u_decode,
        .fields      = output_fields,
};
