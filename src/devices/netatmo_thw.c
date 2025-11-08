/** @file
    NetAtmo outdoor temp/hum and wind sensors.

    Copyright (C) 2025 Klaus Peter Renner

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

/** @fn static int netatmo_thw_decode(r_device *decoder, bitbuffer_t *bitbuffer)
NetAtmo outdoor temperature/humidity sensor and ultrasonic anemometer.

There are several different message types with different message lengths.  
All signals are transmitted with a preamble (multiple) 0xa, followed by the syncword 0xe712,  
 followed by the data length byte and the data segment, and finished by a two byte CRC.  
 CRC16 calculation over all bytes after syncword should result in 0, if there were no bit errors  

 Data rate: 97.600 kbit/s  
 Sync word: 0xe712, e.g. using match=aae712 to eliminate false syncs  

<pre>
 Message Formats (after sync word):  
 ***********************************************  
 Outdoor temp/hum sensor data message:  
 every 50 seconds  
 example:  
 0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 // byte number  
 19 01 5a 91 02 7d ad 57 0d 00 00 00 00 00 00 00 00 35 00 00 00 00 76 00 01 58 69 3c // data  
 |                                                                           |  
  ----------------------------------------------------------------------------- CRC16 range  

 Byte  0            length of message in bytes, 0x19 = 25 bytes  
 Byte  1 - 4        TBD, ID or address, never changing  
 Byte  5            TBD, status information  
 Byte  6            RF signal strength from base (db), signed byte, 0xad = -83 dB, 0x88 = no signal  
 Byte  8 + 7        Battery voltage (0.5 mV), signed short, 0x0d57 = 3415 => 6830 mV  
 Byte  9 - 16       TBD  
 Byte 17            firmware version, 0x35 = 53  
 Byte 18 - 21       TBD  
 Byte 23 + 22       Temperature (0.1 deg C ), signed short, 0x0076 = 118 => 11.8 deg C  
 Byte 24            TBD  
 Byte 25            Relative Humidity in %, unsigned byte, 0x58 = 88 => 88 %  
 Byte 26 + 27       CRC16 with poly=0x8005 and init=0xffff over all data bytes after sync, 26 bytes  

 ***********************************************  
 Outdoor temp/hum sensor status message:  
 every 6 seconds  
 example:  
 0  1  2  3  4  5  6  7  8   // byte number  
 06 01 5a 91 02 7d ad e5 2a // data  
 |                  |  
  -------------------- CRC16 range  

 Byte  0            length of message in bytes, 0x19 = 25 bytes  
 Byte  1 - 4        TBD, ID or address, never changing  
 Byte  5            TBD, status information  
 Byte  6            RF signal strength from base (in dB), signed byte, 0xad = -83 dB, 0x88 = no signal  
 Byte  7 + 8        CRC16 with poly=0x8005 and init=0xffff over all data bytes after sync, 7 bytes  

 ***********************************************  
 Outdoor wind sensor data message:  
 every 6 seconds  
 example:  
 0                   1                   2                   3                   4                   5  
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1  // byte number  
 31015a910300bf161800000000000000001b000000000000022900e4ffd2fff0ff118e1f2a008e1f2a0067ae2900c4af2900a152 // data  
 |                                                                                                  |  
  ---------------------------------------------------------------------------------------------------- CRC16 range  

 Byte  0            length of message in bytes, 0x31 = 49 bytes  
 Byte  1 - 4        TBD, ID or address, never changing  
 Byte  5            TBD, status information  
 Byte  6            RF signal strength from base (db), signed byte, 0xbf = -65 dB, 0x88 = no signal  
 Byte  8 + 7        Battery voltage (1 mV), signed short, 0x1816 = 6166 => 6166 mV  
 Byte  9 - 16       TBD  
 Byte  17           firmware version  
 Byte  18 - 24      TBD  
 Byte 25 + 26       raw 315° windcomponent measurement in 0.1 km/h, short integer little endin  
 Byte 27 + 28       raw 315° windcomponent measurement in 0.1 km/h, short integer little endia  
 Byte 29 + 30       raw 45° windcomponent measurement in 0.1 km/h, short integer little endia  
 Byte 31 + 32       raw 45° windcomponent measurement in 0.1 km/h, short integer little endia  
 Byte 32 - 49       TBD  
 Byte 50 + 51       CRC16 with poly=0x8005 and init=0xffff over all data bytes after sync, 50 bytes  

 ***********************************************  
 base station request message:  
 every 6 seconds  
 example:  
 0  1  2  3  4  5  6  7  8  9  10  // byte number  
 08 00 5A 90 7E 02 B0 03 B1 80 03  // data  
 |                        |  
  -------------------------- CRC16 range  

 Byte  0            length of message in bytes, 0x08 = 8 bytes  
 Byte  1 - 4        TBD, ID or address, never changing  
 Byte  5            requested module id (02 = TH module)  
 Byte  6            RF signal strength of requested module (db), signed byte, 0xb0 = -80 dB, 0x88 = no signal  
 Byte  7            requested module id (03 = anemometer)  
 Byte  8            RF signal strength of requested module (db), signed byte, 0xb1 = -79 dB, 0x88 = no signal  
 Byte  9 + 10       CRC16 with poly=0x8005 and init=0xffff over all data bytes after sync, 9 bytes  
</pre>

 ***********************************************  
To get all raw messages from all NetAtmo sensors:  
rtl_433 -f 868.9M -s 1000k  -R 0 -X 'n=netatmoTHW,m=FSK_PCM,s=8.1,l=8.1,r=800,preamble=aaaae712,match=e712' -M level  

  use "match=e71219" to get only the TH data message  

*/

#include "decoder.h"
#include <math.h>
#include <stdlib.h>


static int netatmo_thw_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {
            0xaa, 0xaa,             // preamble
            0xe7, 0x12,             // sync word
    };
    int id, battery_mV, signal, temp_raw, humidity, raw_a, raw_b, raw_c, raw_d, ws315, ws45, wind_dir;
    float temp_c, wind_speed;
    data_t *data;

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

    /* Only id 0x015a9102 and 0x015a9103  decoding is supported */
    if ((id != 0x015a9102) && (id != 0x015a9103)) return DECODE_ABORT_EARLY;

    signal   = -(256 - b[6]);

        /* clang-format off */
    switch ( b[0] )
    {
      case 6:
        data = data_make(
                "model",        "",             DATA_STRING,    "NetAtmo-TH",
                "id",           "ID Code",      DATA_FORMAT,    "%08x",        DATA_INT,    id,
                "signal_dB",    "Signal",       DATA_FORMAT,    "%d dB",       DATA_INT,    signal,
                "mic",          "Integrity",    DATA_STRING,    "CRC",
                NULL);
        break;
      case 0x19:
        battery_mV  = (b[8] *256 + b[7]) *2;
        temp_raw = (int16_t)((b[23] << 8) | (b[22] )); // sign-extend
        temp_c   = (temp_raw ) * 0.1f;
        humidity = b[25];
        data = data_make(
                "model",         "",            DATA_STRING,    "NetAtmo-TH",
                "id",            "House Code",  DATA_FORMAT,    "%08x",        DATA_INT,    id,
                "battery_mV",    "Battery U",   DATA_FORMAT,    "%d mV",       DATA_INT,    battery_mV,
                "signal_dB",     "Signal",      DATA_FORMAT,    "%d dB",       DATA_INT,    signal,
                "temperature_C", "Temperature", DATA_FORMAT,    "%.01f C",     DATA_DOUBLE, temp_c,
                "humidity",      "Humidity",    DATA_FORMAT,    "%u %%",       DATA_INT,    humidity,
                "mic",           "Integrity",   DATA_STRING,    "CRC",
                NULL);
        break;
      case 0x31:
        battery_mV  = (b[8] *256 + b[7]) ;
        raw_a = (int16_t)((b[26] << 8) | (b[25] )) ; // sign-extend
        raw_b = (int16_t)((b[28] << 8) | (b[27] )) ; // sign-extend
        raw_c = (int16_t)((b[30] << 8) | (b[29] )) ; // sign-extend
        raw_d = (int16_t)((b[32] << 8) | (b[31] )) ; // sign-extend
        ws315 = raw_a + raw_b;
        ws45  = raw_c + raw_d;
        wind_speed = sqrt(ws45*ws45 + ws315*ws315 )*0.05f;
        wind_dir = (short) (atan2f(ws45,ws315) / M_PI * 180 + 315) % 360;

        data = data_make(
                "model",         "",            DATA_STRING,  "NetAtmo-Wind",
                "id",            "ID Code",     DATA_FORMAT,  "%08x",         DATA_INT,    id,
                "battery_mV",    "Battery U",   DATA_FORMAT,  "%d mV",        DATA_INT,    battery_mV,
                "signal_dB",     "Signal",      DATA_FORMAT,  "%d dB",        DATA_INT,    signal,
                "raw_a",         "raw_a 315°",  DATA_FORMAT,  "%d",          DATA_INT,    raw_a,
                "raw_b",         "raw_b 315°",  DATA_FORMAT,  "%d",          DATA_INT,    raw_b,
                "raw_c",         "raw_c 045°",  DATA_FORMAT,  "%d",          DATA_INT,    raw_c,
                "raw_d",         "raw_d 045°",  DATA_FORMAT,  "%d",          DATA_INT,    raw_d,
                "wind_spd_km_h", "Wind Speed",  DATA_FORMAT,  "%.01f km/h",   DATA_DOUBLE, wind_speed,
                "wind_dir_deg",  "Wind Dir",    DATA_FORMAT,  "%u °",        DATA_INT,    wind_dir,
                "mic",           "Integrity",   DATA_STRING,  "CRC",
                NULL);
        break;
      default :
        data = data_make(
                "model",         "",            DATA_STRING,  "NetAtmo-THW",
                "id",            "ID Code",     DATA_FORMAT,  "%08x",        DATA_INT,   id,
                "signal_dB",     "Signal",      DATA_FORMAT,  "%d dB",       DATA_INT,   signal,
                "mic",           "Integrity",   DATA_STRING,  "CRC",
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
        "battery_mV",
        "signal_dB",
        "temperature_C",
        "humidity",
        "wind_spd_km_h",
        "wind_dir_deg",
        "raw_a",
        "raw_b",
        "raw_c",
        "raw_d",
        "mic",
        NULL,
};

r_device const netatmo_thw = {
        .name        = "NetAtmo temp/hum and wind sensors",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 8.1,
        .long_width  = 8.1,
        .reset_limit = 800,
        .decode_fn   = &netatmo_thw_decode,
        .fields      = output_fields,
};

