/* Fine Offset Electronics sensor protocol
 *
 * The protocol is for the wireless Temperature/Humidity sensor
 * Fine Offset Electronics WH2
 * aka Agimex Rosenborg 66796 (sold in Denmark)
 * aka ClimeMET CM9088 (Sold in UK)
 * aka TFA Dostmann/Wertheim 30.3157 (Temperature only!) (sold in Germany)
 * aka ...
 *
 * The sensor sends two identical packages of 48 bits each ~48s. The bits are PWM modulated with On Off Keying
 *
 * The data is grouped in 6 bytes / 12 nibbles
 * [pre] [pre] [type] [id] [id] [temp] [temp] [temp] [humi] [humi] [crc] [crc]
 *
 * pre is always 0xFF
 * type is always 0x4 (may be different for different sensor type?)
 * id is a random id that is generated when the sensor starts
 * temp is 12 bit signed magnitude scaled by 10 celcius
 * humi is 8 bit relative humidity percentage
 * 
 * Based on reverse engineering with gnu-radio and the nice article here:
 *  http://lucsmall.com/2012/04/29/weather-station-hacking-part-2/
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "data.h"
#include "util.h"

static int fineoffset_WH2_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;

    char time_str[LOCAL_TIME_BUFLEN];

    if (debug_output > 1) {
       fprintf(stderr,"Possible fineoffset: ");
       bitbuffer_print(bitbuffer);
    }

    uint8_t id;
    int16_t temp;
    float temperature;
    uint8_t humidity;

    const uint8_t polynomial = 0x31;    // x8 + x5 + x4 + 1 (x8 is implicit)

    // Validate package
    if (bitbuffer->bits_per_row[0] == 48 &&         // Match exact length to avoid false positives
        bb[0][0] == 0xFF &&             // Preamble
        bb[0][5] == crc8(&bb[0][1], 4, polynomial, 0)	// CRC (excluding preamble)
    )
    {
        /* Get time now */
        local_time_str(0, time_str);

         // Nibble 3,4 contains id
        id = ((bb[0][1]&0x0F) << 4) | ((bb[0][2]&0xF0) >> 4);

        // Nibble 5,6,7 contains 12 bits of temperature
        // The temperature is signed magnitude and scaled by 10
        temp = ((bb[0][2] & 0x0F) << 8) | bb[0][3];
        if(temp & 0x800) {
            temp &= 0x7FF;	// remove sign bit
            temp = -temp;	// reverse magnitude
        }
        temperature = (float)temp / 10;

        // Nibble 8,9 contains humidity
        humidity = bb[0][4];


        if (debug_output > 1) {
           fprintf(stderr, "ID          = 0x%2X\n",  id);
           fprintf(stderr, "temperature = %.1f C\n", temperature);
           fprintf(stderr, "humidity    = %u %%\n",  humidity);
        }

        // Thermo
        if (bb[0][4] == 0xFF) {
        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "TFA 30.3157 Temperature sensor",
                         "id",            "ID",          DATA_INT, id,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                          NULL);
        data_acquired_handler(data);
        }
        // Thermo/Hygro
        else {
        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "Fine Offset Electronics, WH2 Temperature/Humidity sensor",
                         "id",            "ID",          DATA_INT, id,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                         "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                          NULL);
        data_acquired_handler(data);
        }
        return 1;
    }
    return 0;
}


/* Fine Offset Electronics WH25 Temperature/Humidity/Pressure sensor protocol
 *
 * The sensor sends a package each ~64 s with a width of ~28 ms. The bits are PCM modulated with Frequency Shift Keying
 *
 * Example:
 * [00] {500} 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 2a aa aa aa aa aa 8b 75 39 40 9c 8a 09 c8 72 6e ea aa aa 80 10 
 * Reading: 22.6 C, 40 %, 1001.7 hPa
 *
 * Extracted data:
 *                   TT TT HH PP PP
 * aa aa aa 2d d4 e5 02 72 28 27 21 c9 bb aa aa aa
 */
static int fineoffset_WH25_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    uint8_t buffer[16];
    uint16_t id = 0;
    float temperature = 0;
    uint8_t humidity = 0;
    float pressure = 0;

    // Validate package
    if (bitbuffer->bits_per_row[0] < 400 || bitbuffer->bits_per_row[0] > 510) {  // Nominal size is 500 bit periods
        return 0;
    }

    /* Get time now */
    local_time_str(0, time_str);

    // Find a data package
    static const uint8_t HEADER[] = { 0xAA, 0xAA, 0xAA, 0x2D };
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 320, HEADER, sizeof(HEADER)*8);    // Normal index is 361, skip some bytes to find faster
    if (bit_offset + sizeof(buffer)*8 >= bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        if (debug_output) {
            fprintf(stderr, "Fineoffset_WH25: short package. Header index: %u\n", bit_offset);
            bitbuffer_print(bitbuffer);
        }
        return 0;
    }

    // Extract relevant bytes
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, buffer, sizeof(buffer)*8);

    id = (uint16_t)buffer[4] << 8 | buffer[5];     // Somewhat guesswork... (Based on 1 sensor)
    temperature = (float)((uint16_t)buffer[6] << 8 | buffer[7]) / 10 - 40.0;
    humidity = buffer[8];
    pressure = (float)((uint16_t)buffer[9] << 8 | buffer[10]) / 10;

    char raw_str[128];
    for (unsigned n=0; n<sizeof(buffer); n++) { sprintf(raw_str+n*3, "%02x ", buffer[n]); }

    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "Fine Offset Electronics, WH25",
                     "id",            "ID",          DATA_INT, id,
                     "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                     "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                     "pressure",      "Pressure",    DATA_FORMAT, "%.01f hPa", DATA_DOUBLE, pressure,
                     "raw",           "raw",         DATA_STRING, raw_str,
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
    NULL
};

static char *output_fields_WH25[] = {
    "time",
    "model",
    "id",
    "temperature_C",
    "humidity",
    "pressure",
    "raw",
    NULL
};

r_device fineoffset_WH2 = {
    .name           = "Fine Offset Electronics, WH-2 Sensor",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 800,	// Short pulse 544µs, long pulse 1524µs, fixed gap 1036µs
    .long_limit     = 2800,	// Maximum pulse period (long pulse + fixed gap)
    .reset_limit    = 2800,	// We just want 1 package
    .json_callback  = &fineoffset_WH2_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};


r_device fineoffset_WH25 = {
    .name           = "Fine Offset Electronics, WH25 Temperature/Humidity/Pressure Sensor",
    .modulation     = FSK_PULSE_PCM,
    .short_limit    = 56,	// Bit width = 56µs
    .long_limit     = 56,	// NRZ encoding (bit width = pulse width)
    .reset_limit    = 20000,	// Package starts with a huge gap of ~18900 us
    .json_callback  = &fineoffset_WH25_callback,
    .disabled       = 1,
    .demod_arg      = 0,
    .fields         = output_fields_WH25
};

