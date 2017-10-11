/* LaCrosse Color Forecast Station (model C85845), or other LaCrosse product
 * utilizing the remote temperature/humidity sensor TX141TH-Bv2 transmitting
 * in the 433.92 MHz band. Product pages:
 * http://www.lacrossetechnology.com/c85845-color-weather-station/
 * http://www.lacrossetechnology.com/tx141th-bv2-temperature-humidity-sensor
 *
 * The TX141TH-Bv2 protocol is OOK modulated PWM with fixed period of 625 us
 * for data bits, preambled by four long startbit pulses of fixed period equal
 * to ~1666 us. Hence, it is similar to Bresser Thermo-/Hygro-Sensor 3CH (bresser_3ch.c
 * included in this source code) with the exception that OOK_PULSE_PWM_TERNARY
 * modulation type is technically more correct than OOK_PULSE_PWM_RAW.
 *
 * A single data packet looks as follows:
 * 1) preamble - 833 us high followed by 833 us low, repeated 4 times:
 *  ----      ----      ----      ----
 * |    |    |    |    |    |    |    |
 *       ----      ----      ----      ----
 * 2) a train of 40 data pulses with fixed 625 us period follows immediately:
 *  ---    --     --     ---    ---    --     ---
 * |   |  |  |   |  |   |   |  |   |  |  |   |   |
 *      --    ---    ---     --     --    ---     -- ....
 * A logical 1 is 417 us of high followed by 208 us of low.
 * A logical 0 is 208 us of high followed by 417 us of low.
 * Thus, in the pictorial example above the bits are 1 0 0 1 1 0 1 ....
 *
 * The TX141TH-Bv2 sensor sends 12 of identical packets, one immediately following
 * the other, in a single burst. These 12-packet bursts repeat every 50 seconds. At
 * the end of the last packet there are two 833 us pulses ("post-amble"?).
 *
 * The data is grouped in 5 bytes / 10 nybbles
 * [id] [id] [flags] [temp] [temp] [temp] [humi] [humi] [chk] [chk]
 *
 * The "id" is an 8 bit random integer generated when the sensor powers up for the
 * first time; "flags" are 4 bits for battery low indicator, test button press,
 * and channel; "temp" is 12 bit unsigned integer which encodes temperature in degrees
 * Celsius as follows:
 * temp_c = temp/10 - 50
 * to account for the -40 C -- 60 C range; "humi" is 8 bit integer indicating
 * relative humidity in %. The method of calculating "chk", the presumed 8-bit checksum
 * remains a complete mystery at the moment of this writing, and I am not totally sure
 * if the last is any kind of CRC. I've run reveng 1.4.4 on exemplary data with all
 * available CRC algorithms and found no match. Be my guest if you want to
 * solve it - for example, if you figure out why the following two pairs have identical
 * checksums you'll become a hero:
 *
 * 0x87 0x02 0x3c 0x3b 0xe1
 * 0x87 0x02 0x7d 0x37 0xe1
 *
 * 0x87 0x01 0xc3 0x31 0xd8
 * 0x87 0x02 0x28 0x37 0xd8
 *
 * Developer's comment 1: because of our choice of the OOK_PULSE_PWM_TERNARY type, the input
 * array of bits will look like this:
 * bitbuffer:: Number of rows: 25
 *  [00] {0} :
 *  [01] {0} :
 *  [02] {0} :
 *  [03] {0} :
 *  [04] {40} 87 02 67 39 f6
 *  [05] {0} :
 *  [06] {0} :
 *  [07] {0} :
 *  [08] {40} 87 02 67 39 f6
 *  [09] {0} :
 *  [10] {0} :
 *  [11] {0} :
 *  [12] {40} 87 02 67 39 f6
 *  [13] {0} :
 *  [14] {0} :
 *  [15] {0} :
 *  [16] {40} 87 02 67 39 f6
 *  [17] {0} :
 *  [18] {0} :
 *  [19] {0} :
 *  [20] {40} 87 02 67 39 f6
 *  [21] {0} :
 *  [22] {0} :
 *  [23] {0} :
 *  [24] {280} 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6
 * which is a direct consequence of two factors: (1) pulse_demod_pwm_ternary() always assuming
 * only one startbit, and (2) bitbuffer_add_row() not adding rows beyond BITBUF_ROWS. This is
 * OK because the data is clearly processable and the unique pattern minimizes the chance of
 * confusion with other sensors, particularly Bresser 3CH.
 * 
 * 2017-10-12 This waste of rows is no longer a problem.  Even in the face of multiple start-bits,
 * the underlying bitbuffer_add_row will not advance past an empty (unused) row.  Also -- the defect
 * wherein the first row [0] was *always* skipped by bitbuffer_add_row has been corrected.  Now, we
 * see:
 * 
 *  [00] {40} 87 02 67 39 f6
 *  [01] {40} 87 02 67 39 f6
 *  [02] {40} 87 02 67 39 f6
 *  [03] {40} 87 02 67 39 f6
 *  [04] {40} 87 02 67 39 f6
 *  ...
 * 
 * Developer's comment 2: with unknown CRC (see above) the obvious way of checking the data
 * integrity is making use of the 12 packet repetition. In principle, transmission errors are
 * be relatively rare, thus the most frequent packet (statistical mode) should represent
 * the true data. Therefore, in the fisrt part of the callback routine the mode is determined
 * for the first 4 bytes of the data compressed into a single 32-bit integer. Since the packet
 * count is small, no sophisticated mode algorithm is necessary; a simple array of <data,count>
 * structures is sufficient. The added bonus is that relative count enables us to determine
 * the quality of radio transmission.
 * 
 * Developer's comment 3: The TX019_Bv0 sends 15 x 37 bits, in the same protocol as the TX141.  The
 * last (37th) bit is perhaps a stop bit indicating the end of the final data bit period.  Thus, the
 * data packet is 1 nibble shorter, but doesn't include the 8-bit humidity field.  I am unsure what
 * data the extra un-accounted for nibble contains.  Also, the battery level field doesn't appear to
 * be valid.
 *                            iiiiiiii sssstttt tttttttt ???????? ?????
 * [00] {37} 0b 92 e9 dd c0 : 00001011 10010010 11101001 11011101 11000  : After battery replaced
 * [00] {37} 0b 92 ed d1 c0 : 00001011 10010010 11101101 11010001 11000  : Subsequent transmission
 * [00] {37} 0b 92 e9 dd c0 : 00001011 10010010 11101001 11011101 11000  : ''
 * 
 * [00] {37} 0b 92 e9 dd c0 : 00001011 10010010 11101001 11011101 11000  : Battery replaced again (not long enough)
 * [00] {37} 0b 92 e9 dd c0 : 00001011 10010010 11101001 11011101 11000  : Subsequent transmission
 * 
 * [00] {37} 75 92 ea 49 c0 : 01110101 10010010 11101010 01001001 11000  : After battery replaced
 * [00] {37} 75 92 eb 4a c0 : 01110101 10010010 11101011 01001010 11000  : Subsequent transmission
 * 
 * [00] {37} 75 92 e9 48 c0 : 01110101 10010010 11101001 01001000 11000  : Spontaneous transmission
 * [00] {37} 75 d2 e9 88 c0 : 01110101 11010010 11101001 10001000 11000  : Test button pushed immediatly
 *                                      ^- test button
 * 
 * [00] {37} 9d 52 f2 39 b0 : 10011101 01010010 11110010 00111001 10110  : Put in bad batteries
 *                                     ^- battery is bad (opposite polarity vs. TX141TH!)
 * 
 * Copyright (C) 2017 Robert Fraczkiewicz   (aromring@gmail.com)
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include "data.h"
#include "rtl_433.h"
#include "util.h"

#define LACROSSE_TX141TH_BITLEN 40 // Temp + Hygro
#define LACROSSE_TX019T_BITLEN 37 // Temp only
#define LACROSSE_TX141TH_BYTELEN 5  // = LACROSSE_TX141TH_BITLEN / 8

static int lacrosse_tx_general_callback(bitbuffer_t *bitbuffer, int version ) {
    bitrow_t *bb = bitbuffer->bb;
    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);
    uint8_t id=0,status=0,battery_low=0,test=0,humidity=0;
    uint16_t temp_raw=0;
    float temp_f,temp_c=0.0;

    // reduce false positives, require at least 5 out of 12/15 repeats.  Locates the
    // most frequent matching row(s), meeting the min_row and min_bit thresholds.
    int bitlength = version == 141 ? LACROSSE_TX141TH_BITLEN : LACROSSE_TX019T_BITLEN;
    int r = bitbuffer_find_repeated_row( bitbuffer, 5, bitlength );
    if ( r < 0 || bitbuffer->bits_per_row[r] > bitlength+1 ) // allow an extra stop-bit
        return 0;

    // Unpack the data bytes
    uint8_t *bytes = bitbuffer->bb[r];
    id=bytes[0];
    status=bytes[1];
    battery_low=(status & 0x80) >> 7;
    test=(status & 0x40) >> 6;
    temp_raw=((status & 0x0F) << 8) + bytes[2];
    temp_f = 9.0*((float)temp_raw)/50.0-58.0; // Temperature in F
    temp_c = ((float)temp_raw)/10.0-50.0; // Temperature in C
    if ( bitbuffer->bits_per_row[r] >= LACROSSE_TX141TH_BITLEN )
        humidity = bytes[3];

    if (0==id || ( 141 == version && 0 == humidity ) || humidity > 100 || temp_f < -40.0 || temp_f > 140.0) {
        if (debug_output) {
            fprintf(stderr, "LaCrosse TX141TH-Bv2 data error\n");
            fprintf(stderr, "id: %i, humidity:%i, temp_f:%f\n", id, humidity, temp_f);
        }
	return 0;
    }

    if ( debug_output )
        fprintf( stdout, "LaCrosse %s: data    = %02X %02X %02X %02X %2X; raw temp: %d\n",
		 141 == version ? "TX141TH-Bv2" : "TX019T-Bv0",
                 bb[r][0], bb[r][1], bb[r][2], bb[r][3], bb[r][4], temp_raw );

    data_t *data;
    if ( 141 == version )
        data = data_make("time",        "Date and time",        DATA_STRING, time_str,
                         "model",       "",                     DATA_STRING, "LaCrosse TX141TH-Bv2 sensor",
                         "id",          "Sensor ID",            DATA_FORMAT, "%02x", DATA_INT, id,
                         "temperature", "Temperature in deg F", DATA_FORMAT, "%.2f F", DATA_DOUBLE, temp_f,
                         "temperature_C","Temperature in deg C",DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                         "humidity",    "Humidity",             DATA_FORMAT, "%u %%", DATA_INT, humidity,
                         "battery",     "Battery",              DATA_STRING, battery_low ? "LOW" : "OK",
                         "test",        "Test?",                DATA_STRING, test ? "Yes" : "No",
                         NULL);
    else
        data = data_make("time",        "Date and time",        DATA_STRING, time_str,
                         "model",       "",                     DATA_STRING, "LaCrosse TX019T-Bv0 sensor",
                         "id",          "Sensor ID",            DATA_FORMAT, "%02x", DATA_INT, id,
                         "temperature", "Temperature in deg F", DATA_FORMAT, "%.2f F", DATA_DOUBLE, temp_f,
                         "temperature_C","Temperature in deg C",DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                         "battery",     "Battery",              DATA_STRING, battery_low ? "OK" : "LOW", // reverse of TX141
                         "test",        "Test?",                DATA_STRING, test ? "Yes" : "No",
                         NULL);

    data_acquired_handler(data);

    return 1;

}

static char *output_fields_TX141TH_Bv2[] = {
    "time",
    "model",
    "id",
    "temperature",
    "temperature_C",
    "humidity",
    "battery",
    "test",
    NULL
};

static char *output_fields_TX019TH_Bv0[] = {
    "time",
    "model",
    "id",
    "temperature",
    "temperature_C",
    "battery",
    "test",
    NULL
};

static int lacrosse_tx141th_bv2_callback(bitbuffer_t *bitbuffer) {
    return lacrosse_tx_general_callback(bitbuffer, 141 );
}

static int lacrosse_tx019t_bv0_callback(bitbuffer_t *bitbuffer) {
    return lacrosse_tx_general_callback(bitbuffer, 19 );
}

r_device lacrosse_TX141TH_Bv2 = {
    .name          = "LaCrosse TX141TH-Bv2 sensor",
    .modulation    = OOK_PULSE_PWM_TERNARY,
    .short_limit   = 312,     // short pulse is ~208 us, long pulse is ~417 us
    .long_limit    = 625,     // long gap (with short pulse) is ~417 us, sync gap is ~833 us
    .reset_limit   = 2000,   // maximum gap is 1250 us (long gap + longer sync gap on last repeat)
    .json_callback = &lacrosse_tx141th_bv2_callback,
    .disabled      = 0,
    .demod_arg     = 2,       // Longest pulses are startbits
    .fields        = output_fields_TX141TH_Bv2,
};

r_device lacrosse_TX019T_Bv0 = {
    .name          = "LaCrosse TX019T-Bv0 sensor",
    .modulation    = OOK_PULSE_PWM_TERNARY,
    .short_limit   = 312,     // short pulse is ~208 us, long pulse is ~417 us
    .long_limit    = 625,     // long gap (with short pulse) is ~417 us, sync gap is ~833 us
    .reset_limit   = 2000,   // maximum gap is 1250 us (long gap + longer sync gap on last repeat)
    .json_callback = &lacrosse_tx019t_bv0_callback,
    .disabled      = 0,
    .demod_arg     = 2,       // Longest pulses are startbits
    .fields        = output_fields_TX019TH_Bv0,
};
