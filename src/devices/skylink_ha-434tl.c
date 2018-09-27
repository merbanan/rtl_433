/* skylink off-brand wireless motion sensor and alarm system on 433.3MHz
 * Skylink HA-434TL
 *

   This motion sensor is pretty primitive, but the price is good.  It only sends messages when it see's motion, and no motion clear message.  It also does not appear to send battery levels or low battery.  It also does not appear to send regular heartbeats as well.

   The message that is received, appears to only be a device serial, and does not appear to change.

   2018-09-26 21:25:07 :	Skylink motion sensor   :	00000
   pulse_demod_ppm(): Skylink HA-434TL motion sensor
   bitbuffer:: Number of rows: 2
   [00] { 0}           :
   [01] {17} be 3e 80  : 10111110 00111110 1
   Pulse data: 19 pulses
   [  0] Pulse:  507, Gap:  498, Period: 1005   Assuming signal header
   [  1] Pulse:  134, Gap:  381, Period:  515   1
   [  2] Pulse:  135, Gap:  126, Period:  261   0
   [  3] Pulse:  134, Gap:  382, Period:  516   1
   [  4] Pulse:  135, Gap:  383, Period:  518   1
   [  5] Pulse:  134, Gap:  382, Period:  516   1
   [  6] Pulse:  135, Gap:  382, Period:  517   1
   [  7] Pulse:  134, Gap:  382, Period:  516   1
   [  8] Pulse:  135, Gap:  125, Period:  260   0
   [  9] Pulse:  135, Gap:  125, Period:  260   0
   [ 10] Pulse:  134, Gap:  126, Period:  260   0
   [ 11] Pulse:  134, Gap:  382, Period:  516   1
   [ 12] Pulse:  134, Gap:  382, Period:  516   1
   [ 13] Pulse:  135, Gap:  381, Period:  516   1
   [ 14] Pulse:  136, Gap:  381, Period:  517   1
   [ 15] Pulse:  136, Gap:  381, Period:  517   1
   [ 16] Pulse:  135, Gap:  125, Period:  260   0
   [ 17] Pulse:  135, Gap:  383, Period:  518   1
   [ 18] Pulse:  134, Gap: 5071, Period: 5205   End of message

   This is transmitted roughly every 2 hours

   {"time" : "2018-09-27 06:55:50", "model" : "Skylink motion sensor", "code" : "5e3e8"}
   pulse_demod_ppm(): Skylink HA-434TL motion sensor
   bitbuffer:: Number of rows: 2
   [00] { 3} e0        : 111
   [01] {17} 5e 3e 80  : 01011110 00111110 1
   Pulse data: 22 pulses
   [  0] Pulse:   12, Gap:  269, Period:  281
   [  1] Pulse:   13, Gap:  269, Period:  282
   [  2] Pulse:   10, Gap:  235, Period:  245
   [  3] Pulse:  506, Gap:  499, Period: 1005  Header
   [  4] Pulse:  132, Gap:  127, Period:  259  0
   [  5] Pulse:  132, Gap:  385, Period:  517  1
   [  6] Pulse:  132, Gap:  127, Period:  259
   [  7] Pulse:  133, Gap:  384, Period:  517
   [  8] Pulse:  132, Gap:  385, Period:  517
   [  9] Pulse:  132, Gap:  385, Period:  517
   [ 10] Pulse:  132, Gap:  384, Period:  516
   [ 11] Pulse:  132, Gap:  128, Period:  260
   [ 12] Pulse:  132, Gap:  128, Period:  260
   [ 13] Pulse:  132, Gap:  127, Period:  259
   [ 14] Pulse:  132, Gap:  384, Period:  516
   [ 15] Pulse:  133, Gap:  384, Period:  517
   [ 16] Pulse:  133, Gap:  383, Period:  516
   [ 17] Pulse:  133, Gap:  384, Period:  517
   [ 18] Pulse:  133, Gap:  383, Period:  516
   [ 19] Pulse:  133, Gap:  128, Period:  261
   [ 20] Pulse:  132, Gap:  384, Period:  516
   [ 21] Pulse:  133, Gap: 5061, Period: 5194
   new_tmplate callback:

 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"
#include "data.h"
#include "util.h"

static int skylink_motion_callback(bitbuffer_t *bitbuffer) {
        char time_str[LOCAL_TIME_BUFLEN];
        data_t *data;
        uint8_t *b;
        int code;
        int motion;
        int raw;
        char code_str[6];
        char raw_str[6];

        if (debug_output > 1) {
                printf("new_tmplate callback:\n");
                bitbuffer_print(bitbuffer);
        }

        for (int i = 0; i < bitbuffer->num_rows; ++i) {
                b = bitbuffer->bb[i];
                if (bitbuffer->bits_per_row[i] != 17)
                        continue;

                {

// [01] {17} 5e 3e 80  : 01011110 00111110 1  -- No motion
// [01] {17} be 3e 80  : 10111110 00111110 1  -- Motion

                        motion = (b[0] >> 5);
                        raw = code = ((b[0]) << 12) | (b[1] << 4) | (b[2] >> 4);
                        code = ((b[0] & 0x1F) << 12) | (b[1] << 4) | (b[2] >> 4);

                        sprintf(code_str, "%05x", code);
                        sprintf(raw_str, "%05x", raw);
                        motion = ( motion == 5 );


                        /* Get time now */
                        local_time_str(0, time_str);
                        data = data_make(
                                "time",     "",  DATA_STRING, time_str,
                                "model",    "",  DATA_STRING, "Skylink HA-434TL motion sensor",
                                "motion",    "",  DATA_STRING, motion ? "true" : "false",
                                "id",     "",  DATA_STRING, code_str,
                                "raw",     "",  DATA_STRING, raw_str,
                                NULL);

                        data_acquired_handler(data);
                        return 1;
                }
        }
        return 0;
}

static char *output_fields[] = {
        "time",
        "model",
        "code",
        NULL
};

r_device skylink_motion = {
        .name           = "Skylink HA-434TL motion sensor",
        .modulation     = OOK_PULSE_PPM_RAW,
<<<<<<< HEAD
        .short_limit    = 600,// Divide by 4 from DEBUG ouput
        .long_limit     = 1700,
        .reset_limit    = 10000,
=======
        .short_limit    = 650,
        .long_limit     = 3000,
        .reset_limit    = 20000,
>>>>>>> origin/master
        .json_callback  = &skylink_motion_callback,
        .disabled       = 0,
        .demod_arg      = 0,
        .fields         = output_fields
};
