/** @file
    LaCrosse TX 433 Mhz Temperature and Humidity Sensors.

    Copyright (C) 2015 Robert C. Terzi

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
LaCrosse TX 433 Mhz Temperature and Humidity Sensors.
- Tested: TX-7U and TX-6U (Temperature only)
- Not Tested but should work: TX-3, TX-4
- also TFA Dostmann 30.3120.90 sensor (for e.g. 35.1018.06 (WS-9015) station)
- also TFA Dostmann 30.3121 sensor

Protocol Documentation: http://www.f6fbb.org/domo/sensors/tx3_th.php

Message is 44 bits, 11 x 4 bit nybbles:

    [00] [cnt = 10] [type] [addr] [addr + parity] [v1] [v2] [v3] [iv1] [iv2] [check]

Notes:
- Zero Pulses are longer (1400 uS High, 1000 uS Low) = 2400 uS
- One Pulses are shorter (550 uS High, 1000 uS Low) = 1600 uS
- Sensor id changes when the battery is changed
- Primary Value are BCD with one decimal place: vvv = 12.3
- Secondary value is integer only intval = 12, seems to be a repeat of primary
  This may actually be an additional data check because the 4 bit checksum
  and parity bit is  pretty week at detecting errors.
- Temperature is in Celsius with 50.0 added (to handle negative values)
- Humidity values appear to be integer precision, decimal always 0.
- There is a 4 bit checksum and a parity bit covering the three digit value
- Parity check for TX-3 and TX-4 might be different.
- Msg sent with one repeat after 30 mS
- Temperature and humidity are sent as separate messages
- Frequency for each sensor may be could be off by as much as 50-75 khz
- LaCrosse Sensors in other frequency ranges (915 Mhz) use FSK not OOK
  so they can't be decoded by rtl_433 currently.
- Temperature and Humidity are sent in different messages bursts.

*/

#include "decoder.h"

#define LACROSSE_TX_BITLEN        44
#define LACROSSE_NYBBLE_CNT        11

static int lacrossetx_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int events = 0;
    int result = 0;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        // break out the message nybbles into separate bytes
        // The LaCrosse protocol is based on 4 bit nybbles.
        uint8_t *pRow = bitbuffer->bb[row];
        uint8_t msg_nybbles[LACROSSE_NYBBLE_CNT];
        int16_t rowlen = bitbuffer->bits_per_row[row];

        // Actual Packet should start with 0x0A and be 6 bytes
        // actual message is 44 bit, 11 x 4 bit nybbles.
        if (rowlen != LACROSSE_TX_BITLEN) {
            result = DECODE_ABORT_LENGTH;
            continue; // DECODE_ABORT_LENGTH
        }
        if (pRow[0] != 0x0a) {
            result = DECODE_ABORT_EARLY;
            continue; // DECODE_ABORT_EARLY
        }

        for (int i = 0; i < LACROSSE_NYBBLE_CNT; i++) {
            msg_nybbles[i] = 0;
        }

        // Move nybbles into a byte array
        // Compute parity and checksum at the same time.
        uint8_t parity = 0;
        for (int i = 0; i < 44; i++) {
            uint8_t rbyte_no = i / 8;
            uint8_t rbit_no = 7 - (i % 8);
            uint8_t mnybble_no = i / 4;
            uint8_t mbit_no = 3 - (i % 4);
            uint8_t bit = (pRow[rbyte_no] & (1 << rbit_no)) ? 1 : 0;
            msg_nybbles[mnybble_no] |= (bit << mbit_no);

            // Check parity on three bytes of data value
            // TX3U might calculate parity on all data including
            // sensor id and redundant integer data
            if (mnybble_no > 4 && mnybble_no < 8) {
                parity += bit;
            }

            //decoder_logf(decoder, 0, __func__, "recv: [%d/%d] %d -> msg [%d/%d] %02x, Parity: %d",
            //        rbyte_no, rbit_no, bit, mnybble_no, mbit_no, msg_nybbles[mnybble_no], parity);
        }

        uint8_t parity_bit = msg_nybbles[4] & 0x01;
        parity += parity_bit;

        // Validate Checksum (4 bits in last nybble)
        uint8_t checksum = 0;
        for (int i = 0; i < 10; i++) {
            checksum = (checksum + msg_nybbles[i]) & 0x0F;
        }

        // decoder_logf(decoder, 0, __func__,"Parity: %d, parity bit %d, Good %d", parity, parity_bit, parity % 2);

        if (checksum != msg_nybbles[10] || (parity % 2 != 0)) {
            decoder_logf(decoder, 2, __func__,
                    "LaCrosse TX Checksum/Parity error: Comp. %d != Recv. %d, Parity %d",
                    checksum, msg_nybbles[10], parity);
            result = DECODE_FAIL_MIC;
            continue; // DECODE_FAIL_MIC
        }

        // TODO: check if message length is a valid value
        //uint8_t msg_len      = msg_nybbles[1];
        uint8_t msg_type       = msg_nybbles[2];
        uint8_t sensor_id      = (msg_nybbles[3] << 3) + (msg_nybbles[4] >> 1);
        uint16_t msg_value_raw = (msg_nybbles[5] << 8) | (msg_nybbles[6] << 4) | msg_nybbles[7];
        float msg_value        = msg_nybbles[5] * 10 + msg_nybbles[6] + msg_nybbles[7] * 0.1f;
        int msg_value_int      = msg_nybbles[8] * 10 + msg_nybbles[9];

        // Check Repeated data values as another way of verifying
        // message integrity.
        if (msg_nybbles[5] != msg_nybbles[8]
                || msg_nybbles[6] != msg_nybbles[9]) {
            decoder_logf(decoder, 1, __func__,
                    "Sensor %02x, type: %d: message value mismatch int(%.1f) != %d?\n",
                    sensor_id, msg_type, msg_value, msg_value_int);
            result = DECODE_FAIL_SANITY;
            continue; // DECODE_FAIL_SANITY
        }

        if (msg_type == 0x00) {
            float temp_c = msg_value - 50.0f;
            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",             DATA_STRING, "LaCrosse-TX",
                    "id",               "",             DATA_INT,    sensor_id,
                    "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "mic",              "Integrity",    DATA_STRING, "PARITY",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            events++;
        }
        else if (msg_type == 0x0E) {
            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",             DATA_STRING, "LaCrosse-TX",
                    "id",               "",             DATA_INT,    sensor_id,
                    "humidity",         "Humidity",     DATA_COND,   msg_value_raw != 0xff, DATA_FORMAT, "%.1f %%", DATA_DOUBLE, msg_value,
                    "mic",              "Integrity",    DATA_STRING, "PARITY",
                    NULL);
            /* clang-format on */
            decoder_output_data(decoder, data);
            events++;
        }
        else  {
            // TODO: this should be reported/counted as exception, not considered debug
            decoder_logf(decoder, 1, __func__,
                    "Sensor %02x: Unknown Reading type %d, % 3.1f (%d)\n",
                    sensor_id, msg_type, msg_value, msg_value_int);
        }
    }

    if (events) {
        return events;
    }

    return result;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device const lacrossetx = {
        .name        = "LaCrosse TX Temperature / Humidity Sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 550,  // 550 us pulse + 1000 us gap is 1
        .long_width  = 1400, // 1400 us pulse + 1000 us gap is 0
        .gap_limit   = 3000, // max gap is 1000 us
        .reset_limit = 8000, // actually: packet gap is 29000 us
        .sync_width  = 0,    // not used
        .decode_fn   = &lacrossetx_decode,
        .fields      = output_fields,
};
