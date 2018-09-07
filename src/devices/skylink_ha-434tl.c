/* skylink off-brand wireless motion sensor and alarm system on 433.3MHz
 * Skylink HA-434TL
 *
 Detected OOK package	@ 2018-09-06 20:07:40
 Analyzing pulses...
 Total count:   19,  width:  8637		(34.5 ms)
 Pulse width distribution:
  [ 0] count:    1,  width:   510 [510;510]	(2040 us)
  [ 1] count:   18,  width:   137 [136;139]	( 548 us)
 Gap width distribution:
  [ 0] count:    1,  width:   493 [493;493]	(1972 us)
  [ 1] count:   12,  width:   379 [378;381]	(1516 us)
  [ 2] count:    5,  width:   122 [122;123]	( 488 us)
 Pulse period distribution:
  [ 0] count:    1,  width:  1003 [1003;1003]	(4012 us)
  [ 1] count:   12,  width:   516 [515;519]	(2064 us)
  [ 2] count:    5,  width:   259 [258;261]	(1036 us)
 Level estimates [high, low]:  15957,    313
 Frequency offsets [F1, F2]:  -22334,      0	(-85.2 kHz, +0.0 kHz)
 Guessing modulation: Pulse Width Modulation with multiple packets
 Attempting demodulation... short_limit: 323, long_limit: 382, reset_limit: 494, sync_width: 0
 pulse_demod_pwm(): Analyzer Device
 bitbuffer:: Number of rows: 2
 [00] { 1} 00        : 0
 [01] {18} ff ff c0  : 11111111 11111111 11
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
    char code_str[6];

    for (int i = 0; i < bitbuffer->num_rows; ++i) {
        b = bitbuffer->bb[i];
        // strictly validate package as there is no checksum
        if ((bitbuffer->bits_per_row[i] == 20)
                && ((b[1] != 0) || (b[2] != 0))
                && count_repeats(bitbuffer, i) >= 3) {

            code = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
            sprintf(code_str, "%05x", code);

            /* Get time now */
            local_time_str(0, time_str);
            data = data_make(
                "time",     "",  DATA_STRING, time_str,
                "model",    "",  DATA_STRING, "Skylink motion sensor",
                "code",     "",  DATA_STRING, code_str,
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
    .short_limit    = 136,
    .long_limit     = 505,
    .reset_limit    = 497,
    .json_callback  = &skylink_motion_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};
