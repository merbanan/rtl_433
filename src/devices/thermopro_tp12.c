/* Thermopro TP-12 Thermometer.
 *
 * Copyright (C) 2017 Google Inc.
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include "data.h"
#include "rtl_433.h"
#include "util.h"

/*
A normal sequence for the TP12:

[00] {0} : 
[01] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[02] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[03] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[04] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[05] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[06] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[07] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[08] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[09] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[10] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[11] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[12] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[13] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[14] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[15] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[16] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
[17] {40} 38 73 21 bb 81 : 00111000 01110011 00100001 10111011 10000001 

Layout appears to be:

[01] {41} 38 73 21 bb 81 80 : 00111000 01110011 00100001 10111011 10000001 1
                              device   temp 1   temp     temp 2   checksum
                                       low bits 1   2    low bits
                                                hi bits

*/

#define BITS_IN_VALID_ROW 40

static int thermopro_tp12_sensor_callback(bitbuffer_t *bitbuffer) {
    int iTemp1, iTemp2, good = -1;
    float fTemp1, fTemp2;
    uint8_t *bytes;
    unsigned int device, value;
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;

    // The device transmits 16 rows, let's check for 3 matching.
    // (Really 17 rows, but the last one doesn't match because it's missing a trailing 1.)
    // Update for TP08: same is true but only 2 rows.
    good = bitbuffer_find_repeated_row(
        bitbuffer, 
        (bitbuffer->num_rows > 5) ? 5 : 2,
        40
    );
    if (good < 0) {
        return 0;
    }

    bytes = bitbuffer->bb[good];
    if (!bytes[0] && !bytes[1] && !bytes[2] && !bytes[3]) {
        return 0; // reduce false positives
    }

    // Note: the device ID changes randomly each time you replace the battery, so we can't early out based on it.
    // This is probably to allow multiple devices to be used at once.  When you replace the receiver batteries
    // or long-press its power button, it pairs with the first device ID it hears.
    device = bytes[0];

    if(debug_output) {
        // There is a mysterious checksum in bytes[4].  It may be the same as the checksum used by the TP-11,
        // which consisted of a lookup table containing, for each bit in the message, a byte to be xor-ed into
        // the checksum if the message bit was 1.  It should be possible to solve for that table using Gaussian
        // elimination, so dump some data so we can try this.

        // This format is easily usable by bruteforce-crc, after piping through | grep raw_data | cut -d':' -f2 
        // bruteforce-crc didn't find anything, though - this may not be a CRC algorithm specifically.
        fprintf(stderr,"thermopro_tp12_raw_data:");
        for(int bit_index = 0; bit_index < 40; ++bit_index){
            fputc(bitrow_get_bit(bytes, bit_index) + '0', stderr);
        }
        fputc('\n', stderr);
    }

    iTemp1 = ((bytes[2] & 0xf0) << 4) | bytes[1];
    iTemp2 = ((bytes[2] & 0x0f) << 8) | bytes[3];

    fTemp1 = (iTemp1 - 200) / 10.;
    fTemp2 = (iTemp2 - 200) / 10.;

    local_time_str(0, time_str);
    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "Thermopro TP12 Thermometer",
                     "id",            "Id",          DATA_FORMAT, "\t %d",   DATA_INT,    device,
                     "temperature_1_C", "Temperature 1 (Food)", DATA_FORMAT, "%.01f C", DATA_DOUBLE, fTemp1,
                     "temperature_2_C", "Temperature 2 (Barbecue)", DATA_FORMAT, "%.01f C", DATA_DOUBLE, fTemp2,
                     NULL);
    data_acquired_handler(data);
    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "temperature_1_C",
    "temperature_2_C",
    NULL
};

/*
Analyzing pulses...
Total count:  714,  width: 273019		(1092.1 ms)
Pulse width distribution:
 [ 0] count:    1,  width:    26 [26;26]	( 104 us)
 [ 1] count:  713,  width:   119 [116;140]	( 476 us)
Gap width distribution:
 [ 0] count:   17,  width:   895 [841;945]	(3580 us)
 [ 1] count:  340,  width:   125 [123;128]	( 500 us)
 [ 2] count:  340,  width:   369 [366;372]	(1476 us)
 [ 3] count:   16,  width:   273 [272;274]	(1092 us)
Pulse period distribution:
 [ 0] count:   17,  width:  1027 [867;1084]	(4108 us)
 [ 1] count:  340,  width:   244 [242;262]	( 976 us)
 [ 2] count:  356,  width:   483 [390;490]	(1932 us)
Level estimates [high, low]:  15891,     83
Frequency offsets [F1, F2]:   18586,      0	(+70.9 kHz, +0.0 kHz)

Those gaps are suspiciously close to 500 us and 1500 us.
*/

r_device thermopro_tp12 = {
    .name          = "Thermopro TP08/TP12 thermometer",
    .modulation    = OOK_PULSE_PPM_RAW,
    // note that these are in microseconds, not samples.
    .short_limit   = 1000,
    .long_limit    = 2000,
    .reset_limit   = 4000,
    .json_callback = &thermopro_tp12_sensor_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields,
};
