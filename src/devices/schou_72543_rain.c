/** @file
    Schou 72543 Day Rain Gauge.

    contributed by Jesper M. Nielsen
    discovered by Jesper M. Nielsen
    based upon ambient_weather.c

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Decode Schou 72543 Rain Gauge, DAY series.

Devices supported:

- Schou 72543 Rain Gauge, DAY Series.

This decoder handles the 433mhz rain-thermometer.

Codes example: {66}50fc467b7f9a832a8, {65}a1f88cf6ff3506550, {70}a1f88cf6ff3506557c
{66}: [ 0 ] [ 1010 0001 1111 1000 ] [ 1000 ] [ 1100 ] [ 1111 0110 ] [ 1111 1111 ] [ 0011 0101 ] [ 0000 0110 ] [ 0101 0101 ] [ 0       ]
{65}:       [ 1010 0001 1111 1000 ] [ 1000 ] [ 1100 ] [ 1111 0110 ] [ 1111 1111 ] [ 0011 0101 ] [ 0000 0110 ] [ 0101 0101 ] [ 0       ]
{70}:       [ 1010 0001 1111 1000 ] [ 1000 ] [ 1100 ] [ 1111 0110 ] [ 1111 1111 ] [ 0011 0101 ] [ 0000 0110 ] [ 0101 0101 ] [ 0111 11 ]
KEY:  [ 0 ] [ IIII IIII IIII IIII ] [ SSSS ] [ NNNN ] [ rrrr rrrr ] [ RRRR RRRR ] [ tttt tttt ] [ TTTT TTTT ] [ CCCC CCCC ] [ 0??? ?? ]
- 0:  Always zero
- ?:  Either 1 or 0
- I:  16 bit random ID. Resets to new value after every battery change
- S:  Status bits
      [ X--- ]: Battery status:  0: OK,  1: Low battery
      [ -X-- ]: Repeated signal: 0: New, 1: Repeat of last message (4 repeats will happen after battery replacement)
      [ --XX ]: Assumed always to be 0
- N:  4 bit running count. Increased by 2 every value incremented by 2 every message, i.e. 0, 2, 4, 6, 8, a, c, e, 0, 2...
- Rr: 16 bit Rainfall in 1/10 millimeters per count. Initial value fff6 = 6552.6 mm rain
      r: lower 8 bit, initializes to f6
      R: Upper 8 bit, initializes to ff
- Tt: 16 bit temperature.
      t: lower 8 bit
      T: Upper 8 bit
- C:  Checksum. Running 8 bit sum of the data left of the checksum.
      E.g. {65}a1f88cf6ff3506'55'0 Checksum is 55 obtained as ( a1 + f8 + 8c + f6 + ff + 35 + 06 ) = 455 i.e. 55

*/

#include "decoder.h"

static int schou_72543_rain_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    /* TO DO
        - Choose the right bitbuffer extraction method
        - 
    */
    // HELP NEEDED HERE ///////////////////////////////////////////////////////////////////////////////
    // How do I best extract the bitbuffer values into b[] while accounting for that row #1 is shifted,
    // and that row #3 has additional data at the end?
    // Option #1:
    //     Preferably I would like to remove the 0 bit of row #1 in the bitbuffer,
    //     and then use then do something like:
    //         bitbuffer_find_repeated_prefix(b, 2, 64);
    //     My understanding is that this would return b[] if the 64 bits exist ín at least two rows.
    // Option #2:
    //     As a slower but more roubist alternative I would extract each row in turn and account for
    //     the differences per loop. This has the added benefit that only one rows data must be intact
    //     I.e. If the MIC check is positive, then calculate values, exit loop and return values.
    // >>>>>

    //Assuming option #1

    printf("\nRestart from begining.c\n");

    // Full data is 3 rows, two are required for data validation
    if (bitbuffer->num_rows < 2){
        return DECODE_ABORT_LENGTH;
    }

    printf(" 2) More than two rows in bitbuffer\n");

    int row = bitbuffer_find_repeated_prefix(bitbuffer, 2, 64);
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    printf(" 3) Found at least two alike rows\n");

    uint8_t *b = bitbuffer->bb[row];

    uint8_t micSum = b[7];              // Checksum as read
    int     calSum = add_bytes(b, 7);   // Checksum as calculated
    //uint8_t calSum = 0;               // Checksum as calculated
    //
    //for (i = 0; i<7; i++){
    //    calSum = calSum + b[i];
    //}

    if (micSum != calSum) {
        decoder_logf_bitrow(decoder, 1, __func__, b, 65, "Checksum error, expected: %02x calculated: %02x", micSum, calSum);
        return DECODE_FAIL_MIC;
    }

    printf(" 4) MIC check passed\n");


    // <<<<<
    // Assuming from here on that b[9] = a1 f8 8c f6 ff 35 06 55 00



    //uint8_t  b[9];
    uint16_t deviceID;
    int      isBatteryLow;
    int      isMessageRepeat;
    int      messageCounter;
    float    rain_mm;
    float    temp_F;
    data_t   *data;


    deviceID        =   (b[0] << 8 ) | b[1];
    isBatteryLow    =   (b[2] & 0x80) >  0;                  // if one, battery is low
    isMessageRepeat =   (b[2] & 0x40) >  0;                  // if one, message is a repeat
    messageCounter  =   (b[2] & 0x0e) >> 1;                  // 3 bit counter (rather than 4 bit incrementing by 2 each time
    rain_mm         =  ((b[4] << 8 ) & b[3]) / 10.0f;
    temp_F          = (((b[6] << 8 ) & b[5]) / 10.0f) - 90;

    /* clang-format off */
    data = data_make(
            "model",          "",             DATA_STRING, "Schou 72543 rain sensor",
            "id",             "ID",           DATA_INT,    deviceID,
            "battery_ok",     "Battery",      DATA_INT,    !isBatteryLow,
            "msg_repeat",     "Pairing_msg",  DATA_INT,    isMessageRepeat,
            "msg_counter",    "Counter",      DATA_INT,    messageCounter,
            "rain_mm",        "Rain",         DATA_FORMAT, "%.1f F", DATA_DOUBLE, rain_mm,
            "temp_F",         "Temperature",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp_F,
            "mic",            "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "msg_repeat",
        "msg_counter",
        "rain_mm",
        "temp_F",
        "mic",
        NULL,
};

r_device const schou_72543_rain = {
        .name        = "Schou 72543 Rain sensor, DAY series",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 972,
        .long_width  = 2680,
        .sync_width  = 7328,
        .reset_limit = 2712,
        .decode_fn   = &schou_72543_rain_decode,
        .fields      = output_fields,
};
