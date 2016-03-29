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
    if (bitbuffer->bits_per_row[0] >= 48 &&         // Don't waste time on a short packages
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

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "temperature_C",
    "humidity",
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



