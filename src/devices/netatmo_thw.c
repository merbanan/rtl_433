/** @file
    NetAtmo outdoor temp/hum and wind sensors.

    Copyright (C) 2025 Klaus Peter Renner

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

/**
NetAtmo outdoor temperature/humidity sensor and ultrasonic anemometer.

There are several different message types with different message lengths.
All signals are transmitted with a preamble (multiple) 0xA, followed by the syncword 0xe712,
 followed by the data length byte and the data segment, and finished by a two byte CRC.
 CRC16 calculation over all bytes after syncword should result in 0, if there were no bit errors

 Data rate: 97.600 kbit/s
 Sync word: E7 12,  using match=aae712 to eliminate false syncs

 Message Formats (after sync word):
 ***********************************************
 Outdoor temp/hum sensor data message:
 every 50 seconds
 example:
 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 // byte number
 19 01 5a 91 02 7d ad 57 0d 00 00 00 00 00 00 00 00 35 00 00 00 00 76 00 01 58 69 3c // data
 |                                                                           |
 `---------------------------------------------------------------------------`-- CRC16 range

 Byte  0            length of message in bytes, 0x19 = 25 bytes
 Byte  1 - 4        TBD, ID or address, never changing
 Byte  5            TBD, status information
 Byte  6            RF status (db), signed byte, 0xad = -83 dB
 Byte  8 + 7        Battery voltage (0.5 mV), signed short, 0x0d57 = 3415 => 6830 mV
 Byte  9 - 16       TBD
 Byte 17            firmware version, 0x35 = 53
 Byte 18 - 21       TBD
 Byte 23 + 22       Temperature (0.1 deg C ), signed short, 0x0076 = 118 => 11.8 deg C
 Byte 24            TBD
 Byte 25            Relative Humidity in %, unsigned byte, 0x58 = 88 => 88 %
 Byte 26 + 27       CRC16 with poly=0x8005 and init=0xFFFF over data after sync, 26 bytes

 ***********************************************
 Outdoor temp/hum sensor status message:
 every 6 seconds
 example:
 0  1  2  3  4  5  6  7  8   // byte number
 06 01 5a 91 02 7d ad e5 2a // data
 |                  |
 `------------------`-- CRC16 range

 Byte  0            length of message in bytes, 0x19 = 25 bytes
 Byte  1 - 4        TBD, ID or address, never changing
 Byte  5            TBD, status information battery
 Byte  6            RF status (db), signed byte, 0xad = -83 dB
 Byte  7 + 8        CRC16 with poly=0x8005 and init=0xFFFF over data after sync, 7 bytes

 ***********************************************
 Outdoor wind sensor data message:
 every 6 seconds
 example:

 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51
 31 01 5a 91 03 00 bf 16 18 00 00 00 00 00 00 00 00 1b 00 00 00 00 00 00 02 29 00 e4 ff d2 ff f0 ff 11 8e 1f 2a 00 8e 1f
 2a 00 67 ae 29 00 c4 af 29 00 a1 52
|                                                                                                                                                   |
 `---------------------------------------------------------------------------------------------------------------------------------------------------`-- CRC16 range

 Byte  0            length of message in bytes, 0x31 = 49 bytes
 Byte  1 - 4        TBD, ID or address, never changing
 Byte  5            TBD, status information
 Byte  6            RF status (db), signed byte, 0xbf = -65 dB
 Byte  8 + 7        Battery voltage (1 mV), signed short, 0x1816 = 6166 => 6166 mV
 Byte  9 - 16       TBD
 Byte  17           firmware version
 Byte  18 - 24      TBD
 Byte 25 + 26       raw 315° wind measurement A in 0.1 km/h, short integer little endian
 Byte 27 + 28       raw 315° wind measurement B in 0.1 km/h, short integer little endian
 Byte 29 + 30       raw 45° wind measurement C in 0.1 km/h, short integer little endian
 Byte 31 + 32       raw 45° wind measurement D in 0.1 km/h, short integer little endian
 Byte 32 - 49       TBD
 Byte 50 + 51       CRC16 with poly=0x8005 and init=0xFFFF over data after sync, 50 bytes

 ***********************************************
 other message, request from base station:
 every 6 seconds
 example:
 0  1  2  3  4  5  6  7  8  9  10  // byte number
 08 00 5A 90 7E 02 B0 03 B1 80 03  // data
 |                        |
 `------------------------`-- CRC16 range

 Byte  0            length of message in bytes, 0x08 = 8 bytes
 Byte  1 - 4        TBD, ID or address, never changing
 Byte  5            TBD, request id (02 = TH sensor)
 Byte  6            TBD, request type (B0 = status, B1 = measurement)
 Byte  7            TBD, request id (03 = anemometer)
 Byte  8            TBD, request type (B0 = status, B1 = measurement)
 Byte  9 + 10       CRC16 with poly=0x8005 and init=0xFFFF over data after sync, 7 bytes

 ***********************************************
To get all raw messages from all NetAtmo sensors:
rtl_433 -f 868.9M -s 1000k  -R 0 -X 'n=netatmoTHW,m=FSK_PCM,s=8,l=8,r=800,preamble=aaaae712,match=e712' -M level

  use "match=e71219" to get only the TH data message

## Usage hints:

This decoder accepts 4 parameters to compensate the offset for the wind raw data. The offset can be retrieved by
storing the raw values under zero wind conditions, e.g. at night, and making an average of the 4 raw components
over long enough time. Then use these averaged component values as parameters.
e.g. average values are a=47,b=-2,c=0,d=0 then start the decoder with:

    rtl_433 -R 290:a=47,b=-2,c=0,d=0

Finally, passing a parameter to this decoder requires specifying it explicitly, which normally disables all other
default decoders.  If you want to pass an option to this decoder without disabling all the other defaults,
the simplest method is to explicitly exclude this one decoder (which implicitly says to leave all other defaults
enabled), then add this decoder back with a parameter.  The command line looks like this:

    rtl_433 -R -290 -R 290:a=47,b=-2,c=0,d=0

*/

#include "decoder.h"
#include <math.h>
#include <stdlib.h>
#include "optparse.h"

#define A_raw_0 36
#define B_raw_0 -14
#define C_raw_0 -26
#define D_raw_0 7

struct netatmo_thw_context {
    int a_raw_0;
    int b_raw_0;
    int c_raw_0;
    int d_raw_0;
};

static void usage(void)
{
    fprintf(stderr,
            "Use -R [protocol_number]:a=123,b=-456,c=789,d=101 to set the offset values\n");
    exit(1);
}

static int netatmo_thw_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {
            0xaa, 0xaa,             // preamble
            0xe7, 0x12,             // sync word
    };
    int id, battery_mV, battery_pct, signal, temp_raw, humidity, a_raw, b_raw, c_raw, d_raw, ws315, ws45, wind_dir;
    float temp_c, wind_speed;
    data_t *data;
    struct netatmo_thw_context *context = decoder_user_data(decoder);

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row = 0;
    // Validate message and reject it as fast as possible : check for preamble
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble, sizeof (preamble) * 8);

    if (start_pos == bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_EARLY; // no preamble detected
    }

    uint8_t len;
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + sizeof (preamble) * 8, &len, 8);


    uint8_t frame[256+2+1] = {0}; // uint8_t max bytes + 2 bytes crc + 1 length byte
    frame[0] = len;

    // Get frame (len don't include the length byte and the crc16 bytes)
    bitbuffer_extract_bytes(bitbuffer, row,
            start_pos + (sizeof (preamble) + 1) * 8,
            &frame[1], (len + 2) * 8);

    decoder_log_bitrow(decoder, 2, __func__, frame, (len + 1) * 8, "frame data");

    uint16_t crc = crc16(frame, len + 1, 0x8005, 0xffff);

    if ((frame[len + 1] << 8 | frame[len + 2]) != crc) {
        decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %04x",
                frame[len + 1] << 8 | frame[len + 2], crc);
        return DECODE_FAIL_MIC;
    }
    uint8_t* b = frame;

    id   = b[1] << 24 | b[2] << 16 | b[3] << 8 | b[4];

    /* Only id 0x015a9102 decoding is supported */
    if ((id != 0x015a9102) && (id != 0x015a9103)) return DECODE_ABORT_EARLY;

    signal   = -(256 - b[6]);

        /* clang-format off */
//    if ( b[0] == 0x06 ) {
    switch ( b[0] )
    {
      case 6:
        data = data_make(
                "model",         "",            DATA_STRING, "NetAtmo-TH",
                "id",            "ID Code",  DATA_FORMAT,  "%08x", DATA_INT,   id,
                "signal_dB",    "Signal",     DATA_FORMAT,  "%d dB", DATA_INT,    signal,
                "mic",              "Integrity",    DATA_STRING, "CRC",
                NULL);
        break;
      case 0x19:
        battery_mV  = (b[8] *256 + b[7]) *2;
        battery_pct = battery_mV < 4800 ? 0: (battery_mV - 1200 *4) / (4*4); 	// 4 cells, full @ 1600mV empty @1200 mV per cell
        temp_raw = (int16_t)((b[23] << 8) | (b[22] )); // sign-extend
        temp_c   = (temp_raw ) * 0.1f;
        humidity = b[25];
        data = data_make(
                "model",         "",            DATA_STRING, "NetAtmo-TH",
                "id",            "House Code",  DATA_FORMAT,  "%08x", DATA_INT,   id,
                "battery_ok",    "Battery OK",     DATA_INT,    !!battery_pct,
                "battery_mV",    "Battery U",     DATA_FORMAT,  "%d mV", DATA_INT,    battery_mV,
                "battery_pct",    "Battery %",     DATA_FORMAT,  "%d %%", DATA_INT,    battery_pct,
                "signal_dB",    "Signal",     DATA_FORMAT,  "%d dB", DATA_INT,    signal,
                "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
                "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                "mic",              "Integrity",    DATA_STRING, "CRC",
                NULL);
        break;
      case 0x31:
        battery_mV  = (b[8] *256 + b[7]) ;
        battery_pct = battery_mV < 4800 ? 0: (battery_mV - 1200 *4) / (4*4); 	// 4 cells, full @ 1600mV empty @1200 mV per cell
        a_raw = (int16_t)((b[26] << 8) | (b[25] )) - context->a_raw_0; // sign-extend
        b_raw = (int16_t)((b[28] << 8) | (b[27] )) - context->b_raw_0; // sign-extend
        c_raw = (int16_t)((b[30] << 8) | (b[29] )) - context->c_raw_0; // sign-extend
        d_raw = (int16_t)((b[32] << 8) | (b[31] )) - context->d_raw_0; // sign-extend
        ws315 = a_raw + b_raw;
        ws45  = c_raw + d_raw;
        wind_speed = sqrt(ws45*ws45 + ws315*ws315 )*0.05f;
        wind_dir = (short) (atan2f(ws45,ws315) / M_PI * 180 + 315) % 360;

        data = data_make(
                "model",         "",            DATA_STRING, "NetAtmo-Wind",
                "id",            "ID Code",  DATA_FORMAT,  "%08x", DATA_INT,   id,
                "battery_ok",    "Battery OK",     DATA_INT,    !!battery_pct,
                "battery_mV",    "Battery U",     DATA_FORMAT,  "%d mV", DATA_INT,    battery_mV,
                "battery_pct",    "Battery %",     DATA_FORMAT,  "%d %%", DATA_INT,    battery_pct,
                "signal_dB",    "Signal",     DATA_FORMAT,  "%d dB", DATA_INT,    signal,
                "a_raw",    "a_raw 45°",  DATA_FORMAT,  "%d", DATA_INT,  a_raw,
                "b_raw",    "b_raw 135°",  DATA_FORMAT,  "%d", DATA_INT,  b_raw,
                "c_raw",    "c_raw 225°",  DATA_FORMAT,  "%d", DATA_INT, c_raw,
                "d_raw",    "d_raw 315°",  DATA_FORMAT,  "%d", DATA_INT, d_raw,
                "wind_spd_km_h", "Wind Speed", DATA_FORMAT, "%.01f km/h", DATA_DOUBLE, wind_speed,
                "wind_dir_deg",  "Wind Dir",    DATA_FORMAT, "%u °", DATA_INT, wind_dir,
                "mic",              "Integrity",    DATA_STRING, "CRC",
                NULL);
        break;
      default :
        data = data_make(
                "model",         "",            DATA_STRING, "NetAtmo-THW",
                "id",            "ID Code",  DATA_FORMAT,  "%08x", DATA_INT,   id,
                "signal_dB",    "Signal",     DATA_INT,    signal,
                "mic",              "Integrity",    DATA_STRING, "CRC",
                NULL);
        break;
    }
        /* clang-format on */


    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "battery_mV",
        "battery_pct",
        "signal_dB",
        "temperature_C",
        "humidity",
        "wind_spd_km_h",
        "wind_dir_deg",
        "a_raw",
        "b_raw",
        "c_raw",
        "d_raw",
        "mic",
        NULL,
};

static float parse_atoiv(char const *str, int def, char const *error_hint)
{
    if (!str) {
        return def;
    }

    if (!*str) {
        return def;
    }

    char *endptr;
    int val = strtol(str, &endptr, 10);

    if (str == endptr) {
        fprintf(stderr, "%sinvalid number argument (%s)\n", error_hint, str);
        exit(1);
    }

    return val;
}

r_device const netatmo_thw;

static r_device *netatmo_thw_create(char *arg)
{
    r_device *r_dev = decoder_create(&netatmo_thw, sizeof(struct netatmo_thw_context));
    if (!r_dev) {
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    struct netatmo_thw_context *context = decoder_user_data(r_dev);


    char *key, *val;
    while (getkwargs(&arg, &key, &val)) {
        key = remove_ws(key);
        val = trim_ws(val);

        if (!key || !*key)
            continue;

        else if (!strcasecmp(key, "a"))
            context->a_raw_0 = (int16_t) parse_atoiv(val, 0, "a: ");
        else if (!strcasecmp(key, "b"))
            context->b_raw_0 = (int16_t) parse_atoiv(val, 0, "b: ");
        else if (!strcasecmp(key, "c"))
            context->c_raw_0 = (int16_t) parse_atoiv(val, 0, "c: ");
        else if (!strcasecmp(key, "d"))
            context->d_raw_0 = (int16_t) parse_atoiv(val, 0, "d: ");

        else {
            fprintf(stderr, "Bad arg, unknown keyword (%s)!\n", key);
            usage();
        }
    }
    fprintf(stderr, "Netatmo THW decoder using raw wind offsets: protocol %s :a=%d,b=%d,c=%d,d=%d\n", r_dev->name,context->a_raw_0,context->b_raw_0,context->c_raw_0,context->d_raw_0);

    return r_dev;
}

r_device const netatmo_thw = {
        .name        = "NetAtmo temp/hum and wind sensors",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 8,
        .long_width  = 8,
        .reset_limit = 800,
        .decode_fn   = &netatmo_thw_decode,
        .create_fn   = &netatmo_thw_create,
        .fields      = output_fields,
};

