/* WG-PB12V1 Temperature Sensor
 * ---
 * Device method to decode a generic wireless temperature probe. Probe marked
 * with WG-PB12V1-2016/11.
 *
 * Format of Packets
 * ---
 * The packet format appears to be similar those the Lacrosse format.
 * (http://fredboboss.free.fr/articles/tx29.php)
 *
 * AAAAAAAA ????TTTT TTTTTTTT ???IIIII HHHHHHHH CCCCCCCC
 *
 * A = Preamble - 11111111
 * ? = Unknown - possibly battery charge
 * T = Temperature - see below
 * I = ID of probe is set randomly each time the device is powered off-on,
 *     Note, base station has and unused "123" symbol, but ID values can be
 *     higher than this.
 * H = Humidity - not used, is always 11111111
 * C = Checksum - CRC8, polynomial 0x31, initial value 0x0, final value 0x0
 *
 * Temperature
 * ---
 * Temperature value is "milli-celcius", ie 1000 mC = 1C, offset by -40 C.
 *
 * 0010 01011101 = 605 mC => 60.5 C
 * Remove offset => 60.5 C - 40 C = 20.5 C
 *
 * Unknown
 * ---
 * Possbible uses could be weak battey, or new battery.
 *
 * At the moment it this device cannot distinguish between a Fine Offset
 * device, see fineoffset.c.
 *
 * Copyright (C) 2015 Tommy Vestermark
 * Modifications Copyright (C) 2017 Ciarán Mooney
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "data.h"
#include "util.h"

static int wg_pb12v1_callback(bitbuffer_t *bitbuffer) {
    /* This function detects if the packet (bitbuffer) is from a WG-PB12V1
     * sensor, and decodes it if it passes the checks.
     */

    bitrow_t *bb = bitbuffer->bb;
    data_t *data;

    char time_str[LOCAL_TIME_BUFLEN];

    uint8_t id;
    int16_t temp;
    float temperature;
    uint8_t humidity;
    char io[49];

    const uint8_t polynomial = 0x31;    // x8 + x5 + x4 + 1 (x8 is implicit)

    // Validate package
    if (bitbuffer->bits_per_row[0] >= 48 &&              // Don't waste time on a short packages
        bb[0][0] == 0xFF &&                              // Preamble
        bb[0][5] == crc8(&bb[0][1], 4, polynomial, 0) && // CRC (excluding preamble)
        bb[0][4] == 0xFF                                 // Humitidy set to 11111111
        ){

        /* Get time now */
        local_time_str(0, time_str);

         // Nibble 7,8 contains id
        id = ((bb[0][3]&0x1F));

        // Nibble 5,6,7 contains 12 bits of temperature
        // The temperature is "milli-celcius", ie 1000 mC = 1C, offset by -40 C.
        temp = ((bb[0][1] & 0x0F) << 8) | bb[0][2];
        temperature = ((float)temp / 10)-40;

        // Populate string array with raw packet bits.
        for (uint16_t bit = 0; bit < bitbuffer->bits_per_row[0]; ++bit){
            if (bb[0][bit/8] & (0x80 >> (bit % 8))){
                io[bit] = 49; // 1
               }
            else {
                io[bit] = 48; // 0
                }
            }
        io[48] = 0; // terminate string array.

        if (debug_output > 1) {
           fprintf(stderr, "ID          = 0x%2X\n",  id);
           fprintf(stderr, "temperature = %.1f C\n", temperature);
        }

        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "WG-PB12V1",
                         "id",            "ID",          DATA_INT, id,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temperature,
                         "io",            "io",          DATA_STRING, io,
                          NULL);
        data_acquired_handler(data);
        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    /* Defines the output files for this device function.
     */
    "time",
    "model",
    "id",
    "temperature_C",
    "io",
    NULL
};

r_device wg_pb12v1 = {
    /* Defines object information for use in other parts of RTL_433.
     */
     .name           = "WG-PB12V1",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 650,	// Short pulse 564µs, long pulse 1476µs, fixed gap 960µs
    .long_limit     = 1550,	// Maximum pulse period (long pulse + fixed gap)
    .reset_limit    = 2500,	// We just want 1 package
    .json_callback  = &wg_pb12v1_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};
