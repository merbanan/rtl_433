/* 
 * Telldus weather station indoor unit.
 * Note that the outdoor unit is same as SwitchDoc Labs WeatherSense FT020T
 * 
 * As the indoor unit receives a message from the outdoor unit,
 * it sends 3 radio messages
 * - Oregon-WGR800
 * - Oregon-THGR810 or Oregon-PCR800
 * - Telldus-FT0385R (this one)
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include "decoder.h"

static const uint8_t preamble_pattern[2] = {0x14, 0xe1}; // 13 bits

static const unsigned char crc_table[256] = {
        0x00, 0x31, 0x62, 0x53, 0xc4, 0xf5, 0xa6, 0x97, 0xb9, 0x88, 0xdb, 0xea, 0x7d, 0x4c, 0x1f, 0x2e,
        0x43, 0x72, 0x21, 0x10, 0x87, 0xb6, 0xe5, 0xd4, 0xfa, 0xcb, 0x98, 0xa9, 0x3e, 0x0f, 0x5c, 0x6d,
        0x86, 0xb7, 0xe4, 0xd5, 0x42, 0x73, 0x20, 0x11, 0x3f, 0x0e, 0x5d, 0x6c, 0xfb, 0xca, 0x99, 0xa8,
        0xc5, 0xf4, 0xa7, 0x96, 0x01, 0x30, 0x63, 0x52, 0x7c, 0x4d, 0x1e, 0x2f, 0xb8, 0x89, 0xda, 0xeb,
        0x3d, 0x0c, 0x5f, 0x6e, 0xf9, 0xc8, 0x9b, 0xaa, 0x84, 0xb5, 0xe6, 0xd7, 0x40, 0x71, 0x22, 0x13,
        0x7e, 0x4f, 0x1c, 0x2d, 0xba, 0x8b, 0xd8, 0xe9, 0xc7, 0xf6, 0xa5, 0x94, 0x03, 0x32, 0x61, 0x50,
        0xbb, 0x8a, 0xd9, 0xe8, 0x7f, 0x4e, 0x1d, 0x2c, 0x02, 0x33, 0x60, 0x51, 0xc6, 0xf7, 0xa4, 0x95,
        0xf8, 0xc9, 0x9a, 0xab, 0x3c, 0x0d, 0x5e, 0x6f, 0x41, 0x70, 0x23, 0x12, 0x85, 0xb4, 0xe7, 0xd6,
        0x7a, 0x4b, 0x18, 0x29, 0xbe, 0x8f, 0xdc, 0xed, 0xc3, 0xf2, 0xa1, 0x90, 0x07, 0x36, 0x65, 0x54,
        0x39, 0x08, 0x5b, 0x6a, 0xfd, 0xcc, 0x9f, 0xae, 0x80, 0xb1, 0xe2, 0xd3, 0x44, 0x75, 0x26, 0x17,
        0xfc, 0xcd, 0x9e, 0xaf, 0x38, 0x09, 0x5a, 0x6b, 0x45, 0x74, 0x27, 0x16, 0x81, 0xb0, 0xe3, 0xd2,
        0xbf, 0x8e, 0xdd, 0xec, 0x7b, 0x4a, 0x19, 0x28, 0x06, 0x37, 0x64, 0x55, 0xc2, 0xf3, 0xa0, 0x91,
        0x47, 0x76, 0x25, 0x14, 0x83, 0xb2, 0xe1, 0xd0, 0xfe, 0xcf, 0x9c, 0xad, 0x3a, 0x0b, 0x58, 0x69,
        0x04, 0x35, 0x66, 0x57, 0xc0, 0xf1, 0xa2, 0x93, 0xbd, 0x8c, 0xdf, 0xee, 0x79, 0x48, 0x1b, 0x2a,
        0xc1, 0xf0, 0xa3, 0x92, 0x05, 0x34, 0x67, 0x56, 0x78, 0x49, 0x1a, 0x2b, 0xbc, 0x8d, 0xde, 0xef,
        0x82, 0xb3, 0xe0, 0xd1, 0x46, 0x77, 0x24, 0x15, 0x3b, 0x0a, 0x59, 0x68, 0xff, 0xce, 0x9d, 0xac};

static uint8_t GetCRC(uint8_t crc, uint8_t *lpBuff, uint8_t ucLen)
{
    while (ucLen) {
        ucLen--;
        crc = crc_table[*lpBuff ^ crc];
        lpBuff++;
    }
    return crc;
}

static int
telldus_weather_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[37];
    for (unsigned int i = 0; i < sizeof(b); i++) {
        b[i] = 0;
    }

    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, sizeof(b) * 8);
    /*
    for (unsigned int i = 0; i < sizeof(b); i++)
    {
	    fprintf(stderr,"%02x ", b[i]);
    }
    fprintf(stderr,"\n");
    */

    uint8_t expected   = b[36];
    uint8_t calculated = GetCRC(0xc0, b, 36);
    if (expected != calculated) {
        if (decoder->verbose) {
            fprintf(stderr, "Checksum error in Telldus Weather message.    Expected: %02x    Calculated: %02x\n", expected, calculated);
            fprintf(stderr, "Message: ");
            bitrow_print(b, 36 * 8);
        }
        return 0;
    }

    // FIXME I really have no idea
    //uint8_t device_id = (b[1] & 0xf0) >> 4;
    //if (device_id != 0x02) {
    //return 0; // not my device
    //}

    // uint8_t serial_no       = (b[1] & 0x0f) << 4 | (b[1] & 0xf0) >> 4; // FIXME I really have no idea
    uint8_t flags           = b[2] & 0x0f;                             // FIXME Just a guess
    uint8_t battery_low     = (flags & 0x08) >> 3;                     // FIXME just a guess
    uint16_t wind_speed_avg = b[3] | ((flags & 0x01) << 8);
    uint16_t gust           = b[4] | ((flags & 0x02) << 7);
    uint16_t wind_direction = b[5] | ((flags & 0x04) << 6);
    // uint16_t unk6           = (b[6] << 8) + b[7];   // FIXME
    // uint16_t unk8           = (b[8] << 8) + b[9];   // FIXME
    // uint16_t rain_rate      = (b[10] << 8) + b[11]; // FIXME Just a guess
    uint16_t rain_1h        = (b[12] << 8) + b[13];
    uint16_t rain_24h       = (b[14] << 8) + b[15];
    uint16_t rain_week      = (b[16] << 8) + b[17];
    uint16_t rain_month     = (b[18] << 8) + b[19];
    uint16_t rain_total     = (b[20] << 8) + b[21];
    // uint16_t rain_total2    = (b[22] << 8) + b[23]; // FIXME this or previous ?
    uint16_t temperature    = ((b[24] & 0x0f) << 8) + b[25];
    uint8_t humidity        = b[26];
    // uint8_t unk27           = b[27]; // FIXME flags ?
    uint8_t humidity_indoor = b[28];
    uint16_t pressure_abs   = (b[29] << 8) + b[30];
    uint16_t pressure_rel   = ((b[31]) << 8) + b[32];
    // uint16_t unk33          = ((b[33]) << 8) + b[34]; // fffa
    // uint8_t unk35           = b[35];                  // fa
    // uint8_t crc             = b[36];

    // Where is myLight and myUV ?
    // Is there temperature in ?

    if (temperature == 0xFF) {
        return 0; //  Bad Data
    }

    if (wind_speed_avg == 0xFF) {
        return 0; //  Bad Data
    }

    double temp_c = (((temperature - 400) / 10) - 32) / 1.8;

    /*
    fprintf(stderr, "device_id = %02x %d\n", device_id, device_id);
    fprintf(stderr, "serial_no = %02x %d\n", serial_no, serial_no);
    fprintf(stderr, "flags = %01x %d\n", flags, flags);
    fprintf(stderr, "battery_low  = %01x %d\n", battery_low, battery_low);
    fprintf(stderr, "wind_speed_avg = %02x %d\n", wind_speed_avg, wind_speed_avg);
    fprintf(stderr, "gust = %02x %d\n", gust, gust);
    fprintf(stderr, "wind_direction = %02x %d\n", wind_direction, wind_direction);
    fprintf(stderr, "Unknown6 = %04x %d\n", unk6, unk6);
    fprintf(stderr, "Unknown8 = %04x %d\n", unk8, unk8);
    fprintf(stderr, "rain_rate = %04x %f mm\n", rain_rate, rain_rate * 0.1);
    fprintf(stderr, "rain_1h = %04x %f mm\n", rain_1h, rain_1h * 0.1);
    fprintf(stderr, "rain_24h = %04x %f mm\n", rain_24h, rain_24h * 0.1);
    fprintf(stderr, "rain_week = %04x %f mm\n", rain_week, rain_week * 0.1);
    fprintf(stderr, "rain_month = %04x %f mm\n", rain_month, rain_month * 0.1);
    fprintf(stderr, "rain_total = %04x %f mm\n", rain_total, rain_total * 0.1);
    fprintf(stderr, "rain_total2 = %04x %d\n", rain_total2, rain_total2);
    fprintf(stderr, "temperature = %04x %d\n", temperature, temperature);
    fprintf(stderr, "humidity = %04x %d %%\n", humidity, humidity);
    fprintf(stderr, "Unknown27 = %02x %d\n", unk27, unk27);
    fprintf(stderr, "humidity_indoor = %04x %d %%\n", humidity_indoor, humidity_indoor);
    fprintf(stderr, "pressure_abs = %04x %f\n", pressure_abs, pressure_abs * 0.1);
    fprintf(stderr, "pressure_rel = %04x %f\n", pressure_rel, pressure_rel * 0.1);
    fprintf(stderr, "Unknown33 = %04x %d\n", unk33, unk33);
    fprintf(stderr, "Unknown35 = %02x %d\n", unk35, unk35);
    fprintf(stderr, "crc = %02x %d\n", crc, crc);
    //fprintf(stderr,"flags2 = %01x %d\n", flags2, flags2 );
    fprintf(stderr, "temp_c = %f\n", temp_c);
    */

    data_t *data = data_make(
            "model", "", DATA_STRING, "Telldus-FT0385R",
            // "device", "Device", DATA_INT, device_id,
            // "id",        "Serial Number",          DATA_INT, mySerial,
            "batterylow", "Battery Low", DATA_INT, battery_low,
            "avewindspeed", "Ave Wind Speed", DATA_FORMAT, "%.1f", DATA_DOUBLE, wind_speed_avg * 0.1,
            "gustwindspeed", "Gust", DATA_FORMAT, "%.1f", DATA_DOUBLE, gust * 0.1,
            "winddirection", "Wind Direction", DATA_INT, wind_direction,
            // "rain_rate", "Rain rate", DATA_FORMAT, "%.1f", DATA_DOUBLE, rain_rate * 0.1,
            "rain_hour", "Rain 1h", DATA_FORMAT, "%.1f", DATA_DOUBLE, rain_1h * 0.1,
            "rain_day", "Rain 24h", DATA_FORMAT, "%.1f", DATA_DOUBLE, rain_24h * 0.1,
            "rain_week", "Rain week", DATA_FORMAT, "%.1f", DATA_DOUBLE, rain_week * 0.1,
            "rain_month", "Rain month", DATA_FORMAT, "%.1f", DATA_DOUBLE, rain_month * 0.1,
            "cumulativerain", "Cum Rain", DATA_FORMAT, "%.1f", DATA_DOUBLE, rain_total * 0.1,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity", "Humidity", DATA_INT, humidity,
            "humidity_2", "Indoor Humidity", DATA_INT, humidity_indoor,
            "pressure", "Pressure", DATA_FORMAT, "%.1f", DATA_DOUBLE, pressure_abs * 0.1,
            "pressure_rel", "Pressure Rel", DATA_FORMAT, "%.1f", DATA_DOUBLE, pressure_rel * 0.1,
            "mic", "Integrity", DATA_STRING, "CRC",
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static int
telldus_weather_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row;
    unsigned bitpos;
    int events = 0;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                        (const uint8_t *)&preamble_pattern, 13)) +
                        36 * 8 <=
                bitbuffer->bits_per_row[row]) {
            events += telldus_weather_decode(decoder, bitbuffer, row, bitpos + 8);
            if (events)
                return events; // for now, break after first successful message
            bitpos += 37 * 8;
        }
    }

    return events;
}

static char *output_fields[] = {
        "model",
        //   "device",
        //   "id",
        "batterylow",
        "avewindspeed",
        "gustwindspeed",
        "winddirection",
        // "rain_rate",
        "rain_hour"
        "rain_day",
        "rain_week",
        "rain_month",
        "cumulativerain",
        "temperature_C",
        "humidity",
        "humidity_2",
        "pressure",
        "pressure_rel",
        "mic",
        NULL};

r_device telldus_ft0385r = {
        .name        = "Telldus weather station FT0385R sensors",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 488,
        .long_width  = 0, // not used
        .reset_limit = 2400,
        .decode_fn   = &telldus_weather_callback,
        .disabled    = 0,
        .fields      = output_fields};
