/* Solight TE44
 *
 * Generic wireless thermometer of chinese provenience, which might be sold as part of different kits.
 *
 * So far these were identified (mostly sold in central/eastern europe)
 * - Solight TE44
 * - Solight TE66
 * - EMOS E0107T
 *
 * Rated -50 C to 70 C, frequency 433,92 MHz, three selectable channels.
 *
 * ---------------------------------------------------------------------------------------------
 *
 * Data structure:
 *
 * 12 repetitions of the same 36 bit payload, 1bit zero as a separator between each repetition.
 *
 * 36 bit payload format: xxxxxxxx 10ccmmmm tttttttt 1111hhhh hhhh
 *
 * x - random key - changes after device reset - 8 bits
 * c - channel (0-2) - 2 bits
 * m - multiplier - signed integer, two's complement - 4 bits
 * t - temperature in celsius - unsigned integer - 8 bits
 * h - checksum - 8 bits
 *
 * Temperature in C = ((256 * m) + t) / 10
 *
 * ----------------------------------------------------------------------------------------------
 *
 * Copyright (C) 2017 Miroslav Oujesky
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Perry Kundert 2017-10-13 Amended Data Format:
 * 
 * The temperature is actually a 12-bit signed value:
 * 
 * Examples 36-bit messages:
 * 
 * Replacing battery:   Increase temperature:    Decrease temperature:   
 *   A6 80 D9 F5 A        70 80 DE FD 4            70 80 2A F4 F =   4.2C
 *   A7 80 D8 F3 5        70 81 27 F4 9            70 8F C9 FA 5 = - 5.5C
 *   70 80 DA F5 8        70 81 BE FC 7 = 44.6C    70 8F AA FD D = - 8.6C
 * 
 * 36 bit payload format: xxxxxxxx 10cctttt tttttttt 1111hhhh hhhh
 * 
 * x ID     in nibbles 0-1; 8-bit value
 * t Temp.  in nibbles 3-5; 3 x 4 == 12-bit signed value, 10th of a degree
 * 0b10     in nibble  2  ; unknown 1 value, always seems to be 0b10
 * 0b1111   in nibble  6  ; unknown 2 value, always seems to be 0b1111
 * h Parity in nibbles 7-8; 8-bit value (unknown encoding)
 * 
 */

#include "data.h"
#include "rtl_433.h"
#include "util.h"

static int solight_te44_callback(bitbuffer_t *bitbuffer) {

    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    uint8_t id;
    uint8_t channel;

    bitrow_t *bb = bitbuffer->bb;
    int r = bitbuffer_find_repeated_row( bitbuffer, 3, 36 );
    if ( r < 0 // less than 3 duplicate 36-bit records found
         || bitbuffer->bits_per_row[r] > 37 ) // Row may be 36 or 37 bits (short pulse between end/start bit)
        return 0;

    /*
     * This code used to work, evidently.  However, something in the decoding chain
     * changed, causing the first row pulse (followed by a ~3800ms gap) to issue
     * an empty record for record 0.
     * 

    // simple payload structure check (as the checksum algorithm is still unclear)
    if (bitbuffer->num_rows != 12) {
        return 0;
    }

    for (int i = 0; i < 12; i++) {
        int bits = i < 11 ? 37 : 36; // last line does not contain single 0 separator

        // all lines should have correct length
        if (bitbuffer->bits_per_row[i] != bits) {
            return 0;
        }

        // all lines should have equal content
        // will work also for the last, shorter line, as the separating bit is allways 0 anyway
        if (i > 0 && 0 != memcmp(bb[i], bb[i - 1], BITBUF_COLS)) {
            return 0;
        }
    }

     *
     *
     */

    if ( debug_output >= 1 )
        fprintf( stdout, "solight:  data    = %02X %02X %02X %02X %1X\n",
                 bb[r][0], bb[r][1], bb[r][2], bb[r][3], bb[r][4] >> 4 );

    local_time_str(0, time_str);

    id = bb[r][0];

    channel = (uint8_t) ((bb[r][1] & 0x30) >> 4);

    /*
     * This temperature calculation is unlikely to be correct...  The "multiplier" is actually the
     * upper 4 bits of a 12-bit signed temperature.
     * 
    int8_t multiplier;
    uint8_t temperature_raw;
    // multiplier is 4bit signed value in two's complement format
    // we need to pad with 1s if it is a negative number (starting with 1)
    multiplier = (int8_t) (bb[r][1] & 0x0F);

    if ((multiplier & 0x08) > 0) {
        multiplier |= 0xF0;
    }
    temperature_raw = (uint8_t) bb[r][2];

    temperature = (float) (((256 * multiplier) + temperature_raw) / 10.0);
     *
     *
     */
    // Get 12 bit signed value into high-order bits of 16-bit signed value, shift and sign-extend
    // back into low-order 12 bits.
    int16_t temperature_raw = (  ((uint16_t)(bb[r][1] & 0x0F)) << 12
                               | ((uint16_t)(bb[r][2] & 0xFF)) << 4 );
    temperature_raw >>= 4;
    double temperature = temperature_raw / 10.0;
    
    data = data_make("time", "", DATA_STRING, time_str,
                     "model", "", DATA_STRING, "Solight TE44",
                     "id", "Id", DATA_INT, id,
                     "channel", "Channel", DATA_INT, channel + 1,
                     "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temperature,
                     NULL);
    data_acquired_handler(data);

    return 1;

}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    "temperature_C",
    NULL
};

/*
 * 
 *   Short pulse (32µs), to get a falling edge for Message Start Gap
 *   | Message Start Gap 3900µs (~8x smallest Gap)
 *   | |      Signal 500µs duration
 *   | |      |  Long Gap (1950µs (~4x smallest Gap): "1" Bit
 *   | |      |  |  Signal
 *   | |      |  |  |  Short Gap( 970µs (~2x smallest Gap)
 *   | |      |  |  |  |                     Signal terminating last bit
 *   | |      |  |  |  |                     | End Message Gap; 480µs (~1x) before next Message Start
 *   | |      |  |  |  |                     | |  Message Start Gap 3900µs (~8x)
 *   | v      |  |  |  |                     | |  |
 *   v 1950µs v  v  v  v                     v v  v
 *   |--------||----||--||--|| ... ||----||--||-||--------||---|| ... 
 *             ^ from falling edge to rising ^             ^ repeats 11 more times
 *              \----- 36-bit message ------/               \----- 36-bit message 
 */
r_device solight_te44 = {
    .name           = "Solight TE44, Fuzhou Emax WEC-1502 Temperature (Taylor, Accutemp, ...)",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 1500, // Short gap 970µs (2x), Long gap 1950µs (4x)
    .long_limit     = 3000, // Gap after sync pulse is 3900µs (8x)
    .reset_limit    = 6000, //   stop harvesting repeats if gap exceeds 3900µs (8x)
    .json_callback  = &solight_te44_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};
