/* LaCrosse TX 433 Mhz Temperature and Humidity Sensors
 * Tested: TX-7U and TX-6U (Temperature only)
 *
 * Not Tested but should work: TX-3, TX-4
 *
 * Copyright (C) 2015 Robert C. Terzi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Protocol Documentation: http://www.f6fbb.org/domo/sensors/tx3_th.php
 *
 * Message is 44 bits, 11 x 4 bit nybbles:
 *
 * [00] [cnt = 10] [type] [addr] [addr + parity] [v1] [v2] [v3] [iv1] [iv2] [check]
 *
 * Notes:
 * - Zero Pulses are longer (1,400 uS High, 1,000 uS Low) = 2,400 uS
 * - One Pulses are shorter (  550 uS High, 1,000 uS Low) = 1,600 uS
 * - Sensor id changes when the battery is changed
 * - Primay Value are BCD with one decimal place: vvv = 12.3
 * - Secondary value is integer only intval = 12, seems to be a repeat of primary
 *   This may actually be an additional data check because the 4 bit checksum
 *   and parity bit is  pretty week at detecting errors.
 * - Temperature is in Celsius with 50.0 added (to handle negative values)
 * - Humidity values appear to be integer precision, decimal always 0.
 * - There is a 4 bit checksum and a parity bit covering the three digit value
 * - Parity check for TX-3 and TX-4 might be different.
 * - Msg sent with one repeat after 30 mS
 * - Temperature and humidity are sent as separate messages
 * - Frequency for each sensor may be could be off by as much as 50-75 khz
 * - LaCrosse Sensors in other frequency ranges (915 Mhz) use FSK not OOK
 *   so they can't be decoded by rtl_433 currently.
 *
 * TO DO:
 * - Now that we have a demodulator that isn't stripping the first bit
 *   the detect and decode could be collapsed into a single reasonably
 *   readable function.
 *
 * - Make the time stamp output a generat utility function.
 */

#include "rtl_433.h"
#include "util.h"
#include "data.h"

#define LACROSSE_TX_BITLEN        44
#define LACROSSE_NYBBLE_CNT        11


// Check for a valid LaCrosse TX Packet
//
// Return message nybbles broken out into bytes
// for clarity.  The LaCrosse protocol is based
// on 4 bit nybbles.
//
// Domodulation
// Long bits = 0
// short bits = 1
//
static int lacrossetx_detect(uint8_t *pRow, uint8_t *msg_nybbles, int16_t rowlen) {
    int i;
    uint8_t rbyte_no, rbit_no, mnybble_no, mbit_no;
    uint8_t bit, checksum, parity_bit, parity = 0;

    // Actual Packet should start with 0x0A and be 6 bytes
    // actual message is 44 bit, 11 x 4 bit nybbles.
    if (rowlen == LACROSSE_TX_BITLEN && pRow[0] == 0x0a) {

        for (i = 0; i < LACROSSE_NYBBLE_CNT; i++) {
            msg_nybbles[i] = 0;
        }

        // Move nybbles into a byte array
        // Compute parity and checksum at the same time.
        for (i = 0; i < 44; i++) {
            rbyte_no = i / 8;
            rbit_no = 7 - (i % 8);
            mnybble_no = i / 4;
            mbit_no = 3 - (i % 4);
            bit = (pRow[rbyte_no] & (1 << rbit_no)) ? 1 : 0;
            msg_nybbles[mnybble_no] |= (bit << mbit_no);

            // Check parity on three bytes of data value
            // TX3U might calculate parity on all data including
            // sensor id and redundant integer data
            if (mnybble_no > 4 && mnybble_no < 8) {
                parity += bit;
            }

            //            fprintf(stdout, "recv: [%d/%d] %d -> msg [%d/%d] %02x, Parity: %d %s\n", rbyte_no, rbit_no,
            //                    bit, mnybble_no, mbit_no, msg_nybbles[mnybble_no], parity,
            //                    ( mbit_no == 0 ) ? "\n" : "" );
        }

        parity_bit = msg_nybbles[4] & 0x01;
        parity += parity_bit;

        // Validate Checksum (4 bits in last nybble)
        checksum = 0;
        for (i = 0; i < 10; i++) {
            checksum = (checksum + msg_nybbles[i]) & 0x0F;
        }

        // fprintf(stdout,"Parity: %d, parity bit %d, Good %d\n", parity, parity_bit, parity % 2);

        if (checksum == msg_nybbles[10] && (parity % 2 == 0)) {
            return 1;
        } else {
            if (debug_output > 1) {
                fprintf(stdout,
                        "LaCrosse TX Checksum/Parity error: Comp. %d != Recv. %d, Parity %d\n",
                        checksum, msg_nybbles[10], parity);
            }
            return 0;
        }
    }

    return 0;
}

// LaCrosse TX-6u, TX-7u,  Temperature and Humidity Sensors
// Temperature and Humidity are sent in different messages bursts.
static int lacrossetx_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;

    int i, m, valid = 0;
    int events = 0;
    uint8_t *buf;
    uint8_t msg_nybbles[LACROSSE_NYBBLE_CNT];
    uint8_t sensor_id, msg_type, msg_len, msg_parity, msg_checksum;
    int msg_value_int;
    float msg_value = 0, temp_c = 0;
    time_t time_now;
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;

    for (m = 0; m < BITBUF_ROWS; m++) {
        valid = 0;
        // break out the message nybbles into separate bytes
        if (lacrossetx_detect(bb[m], msg_nybbles, bitbuffer->bits_per_row[m])) {

            msg_len = msg_nybbles[1];
            msg_type = msg_nybbles[2];
            sensor_id = (msg_nybbles[3] << 3) + (msg_nybbles[4] >> 1);
            msg_parity = msg_nybbles[4] & 0x01;
            msg_value = msg_nybbles[5] * 10 + msg_nybbles[6]
                + msg_nybbles[7] / 10.0;
            msg_value_int = msg_nybbles[8] * 10 + msg_nybbles[9];
            msg_checksum = msg_nybbles[10];

            time(&time_now);
            local_time_str(0, time_str);

            // Check Repeated data values as another way of verifying
            // message integrity.
            if (msg_nybbles[5] != msg_nybbles[8] ||
                msg_nybbles[6] != msg_nybbles[9]) {
                if (debug_output) {
                    fprintf(stderr,
                            "LaCrosse TX Sensor %02x, type: %d: message value mismatch int(%3.1f) != %d?\n",
                            sensor_id, msg_type, msg_value, msg_value_int);
                }
                continue;
            }

            switch (msg_type) {
            case 0x00:
                temp_c = msg_value - 50.0;
                data = data_make("time",          "",            DATA_STRING, time_str,
                                 "model",         "",            DATA_STRING, "LaCrosse TX Sensor",
                                 "id",            "",            DATA_INT, sensor_id,
                                 "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                                 NULL);
                data_acquired_handler(data);
                events++;
                break;

            case 0x0E:
                data = data_make("time",          "",            DATA_STRING, time_str,
                                 "model",         "",            DATA_STRING, "LaCrosse TX Sensor",
                                 "id",            "",            DATA_INT, sensor_id,
                                 "humidity",      "Humidity", DATA_FORMAT, "%.1f %%", DATA_DOUBLE, msg_value,
                                 NULL);
                data_acquired_handler(data);
                events++;
                break;

            default:
                // @todo this should be reported/counted as exception, not considered debug
                if (debug_output) {
                    fprintf(stderr,
                            "%s LaCrosse Sensor %02x: Unknown Reading type %d, % 3.1f (%d)\n",
                            time_str, sensor_id, msg_type, msg_value, msg_value_int);
                }
            }
        }
    }

    return events;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "temperature_C",
    "humidity",
    NULL
};

r_device lacrossetx = {
    .name           = "LaCrosse TX Temperature / Humidity Sensor",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 952,
    .long_limit     = 3000,
    /// .reset_limit    = 32000,
    .reset_limit    = 8000,
    .json_callback  = &lacrossetx_callback,
    .disabled       = 0,
    .demod_arg      = 0,  // No Startbit removal
    .fields = output_fields,
};
