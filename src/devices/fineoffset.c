/* Fine Offset Electronics sensor protocol
 *
 * The protocol is for the wireless Temperature/Humidity sensor 
 * Fine Offset Electronics WH2
 * aka Agimex Rosenborg 66796 (sold in Denmark)
 * aka ClimeMET CM9088 (Sold in UK)
 * aka ...
 *
 * The sensor sends two identical packages of 48 bits each ~50s. The bits are PWM modulated with On Off Keying
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


// Generic CRC-8
// (Should probably be moved to somewhere common)
// polynomial byte is from x^7 to x^0 (x^8 is implicitly one)
uint8_t crc8(uint8_t const message[], unsigned nBytes, uint8_t polynomial) {
    uint8_t remainder = 0;	
    unsigned byte, bit;

    for (byte = 0; byte < nBytes; ++byte) {
        remainder ^= message[byte];
        for (bit = 0; bit < 8; ++bit) {
            if (remainder & 0x80) {
                remainder = (remainder << 1) ^ polynomial;
            }
            else {
                remainder = (remainder << 1);
            }
        }
    }
    return remainder;
}


static int fineoffset_WH2_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]) {
    uint8_t ID;
    float temperature;
    float humidity;

    const uint8_t polynomial = 0x31;    // x8 + x5 + x4 + 1 (x8 is implicit)

    // Validate package
    if (bits_per_row[0] >= 48 &&        // Dont waste time on a short package
        bb[0][0] == 0xFF &&             // Preamble
	    bb[0][5] == crc8(&bb[0][1], 4, polynomial)	// CRC (excluding preamble)
	) 
    {
	    // Nibble 3,4 contains ID
        ID = ((bb[0][1]&0x0F) << 4) | ((bb[0][2]&0xF0) >> 4);

        // Nible 5,6,7 contains 12 bits of temperature
        // The temperature is signed magnitude and scaled by 10
        int16_t temp;
        temp = bb[0][3];
        temp |= (int16_t)(bb[0][2] & 0x0F) << 8;
        if(temp & 0x800) {
            temp &= 0x7FF;	// remove sign bit
            temp = -temp;	// reverse magnitude
        }
        temperature = (float)temp / 10;

        // Nibble 8,9 contains humidity
        humidity = bb[0][4];

        fprintf(stderr, "Fine Offset Electronics, WH2:\n");
        fprintf(stderr, "ID          = 0x%2X\n", ID);
        fprintf(stderr, "temperature = %.1f C\n", temperature);
        fprintf(stderr, "humidity    = %2.0f %%\n", humidity);
        // fprintf(stderr, "raw         = %02x %02x %02x %02x %02x %02x\n",bb[0][0],bb[0][1],bb[0][2],bb[0][3],bb[0][4],bb[0][5]);

        if (debug_output)
            debug_callback(bb, bits_per_row);

        return 1;
    }
    return 0;
}


r_device fineoffset_WH2 = {
    /* .id             = */ 12,
    /* .name           = */ "Fine Offset Electronics, WH-2 Sensor",
    /* .modulation     = */ OOK_PWM_RAW,
    /* .short_limit    = */ 200,	// Short pulse 136, long pulse 381, fixed gap 259
    /* .long_limit     = */ 700,	// Maximum pulse period (long pulse + fixed gap)
    /* .reset_limit    = */ 700,	// We just want 1 package
    /* .json_callback  = */ &fineoffset_WH2_callback,
};



