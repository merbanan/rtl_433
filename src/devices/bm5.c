/** @file
    bm5-v2 12V Automotive Wireless Battery Monitor.

    Copyright (C) 2025 Cameron Murphy

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
bm5-v2 12V Automotive Battery Monitor.

Sold as "ANCEL BM200" on Amazon, and "QUICKLYNKS BM5-D" on AliExpress

The sensor transmits a single message, with all relevant data about every 1-2
seconds at 433.92 MHz,

The transmission is inverted from the normal OOK_PULSE_PWM decoder, with a "0"
represented as a short pulse of 225us, and a 675us gap, and a "1" represented as
a long 675us pulse, and a 225us gap.  The implementation below initially
inverters the buffer to correct for this.

Each message consists of a preamble (long pulse, plus eight 50% symbol length
pulses) sent at double the normal data rate, then a one byte pause (at regular
data rate), then ten bytes of payload, plus a one byte Checksum.  The preamble
is decoded as (0x7F 0x80) by rtl_433 (in the native, non-inverted state) due to
the initial pulse.

Flex decoder:  `rtl_433 -R 0 -X 'n=bm5-v2,m=OOK_PWM,s=227,l=675,r=6000,invert'`


Payload:

- I = 3 byte ID
- S = 7 bits for battery State of Health (SOH) - 0 to 100 percent
- C = 1 bit flag for charging system error (!CHARGING on display --probably
triggered if running voltage below ~13v)
- s = 7 bits for battery State of Charge (SOC) 0 to 100 percent
- c = 1 bit flag for cranking system error. (!CRANKING indicator on display -
triggered if starting voltage drops for too long -- excessive cranking)
- T = 7 bits for sensor temperature magnitude (degrees C, converted if necessary
in display)
- t = 1 bit for temperature sign (0 = positive, 1 = negative)
- V = 16 bits, little endian for current battery voltage (Voltage is displayed
as a float with 2 significant digits.  The 16 bit int represents this voltage,
multiplied by 1600. -- note:  The display truncates the voltages to 2 decimal
points.  I've chosen to round instead of truncate, as this seems a better
representation of the true value.)
- v = 16 bits, little endian for previous low voltage during last start.  (Is
probably used for the algorithm to determine battery health.  This value will be
closer to resting voltage for healthy batteries) Same 1600 multiplier as above.
- R = 1 byte Checksum

    msg:
IIIIIIIIIIIIIIIIIIIIIIIISSSSSSSCssssssscTTTTTTTtVVVVVVVVVVVVVVVVvvvvvvvvvvvvvvvvRRRRRRRR
    ID:24h SOH:7d CHARGING:1b SOC:7d CRANKING:1b TEMP:8s V_CUR:16d V_START:16d
CHECKSUM:8h

*/

#include "decoder.h"

static int bm5_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[11];

    bitbuffer_invert(bitbuffer); // This device sends data inverted relative to
                                 // the OOK_PWM decoder output.

    if (bitbuffer->num_rows != 1) { // Only one message per transmission
        return DECODE_ABORT_EARLY;
    }

    // check correct data length
    if (bitbuffer->bits_per_row[0] != 88) { // 10 bytes data + 1 byte checksum)
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, 0, b, sizeof(b) * 8);

    // reduce false positives
    if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[10] == 0) {
        return DECODE_FAIL_MIC; // false positive
    }

    // check for valid checksum
    if ((unsigned char)add_bytes(&b[0], 10) != b[10]) {
        return DECODE_FAIL_MIC; // failed checksum - invalid message
    }

    int id             = (b[0] << 16) | (b[1] << 8) | b[2];
    int soh            = b[3] >> 1;          // State of Health encoded in 1st 7 bits
    int charging_error = b[3] & 0x01;        // Charging error flag in bit 8 of byte 4
    int soc            = b[4] >> 1;          // State pf Charge encoded in 1st 7 bits
    int cranking_error = b[4] & 0x01;        // Cranking error flag in bit 8 of byte 5
    int temp           = b[5] >> 1;          // Temperature magnitude in degrees C in first 7 bits of byte 6
    int temp_sign      = b[5] & 0x01;        // Temperature sign in bit 8 of byte 6
    int volt1          = (b[7] << 8) | b[6]; // Current voltage
    int volt2          = (b[9] << 8) | b[8]; // Previous starting voltage
                                             //
    if (temp_sign == 1) {
        temp = -temp; // Invert temp value
    }

    float battery_volt = volt1 * 0.000625f; // Convert transmitted values to floats.  Rounded to 2
                                            // decimal places in "data_make" below.
    float starting_volt = volt2 * 0.000625f;

    /* clang-format off */
    data = data_make(
            "model",            "",                       DATA_STRING,   "BM5-v2",
            "id",               "Device_ID",              DATA_FORMAT,   "%X",            DATA_INT,          id,
            "health_pct",       "State of Health",        DATA_FORMAT,   "%d %%",         DATA_INT,          soh,
            "cranking_error",   "Cranking System Error",  DATA_INT,       cranking_error,
            "charge_pct",       "State of Charge",        DATA_FORMAT,   "%d %%",         DATA_INT,          soc,
            "charging_error",   "Charging System Error",  DATA_INT,       charging_error,
            "temperature_C",    "Temperature",            DATA_FORMAT,   "%.1f C",        DATA_DOUBLE,       (float) temp,
            "battery_V",        "Current Battery Voltage",DATA_FORMAT,   "%.2f V",        DATA_DOUBLE,       battery_volt,
            "starting_V",       "Starting Voltage",       DATA_FORMAT,   "%.2f V",        DATA_DOUBLE,       starting_volt,
            "mic",              "Integrity",              DATA_STRING,   "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "health_pct",
        "cranking_error",
        "charge_pct",
        "charging_error",
        "temperature_C",
        "battery_V",
        "starting_V",
        "mic",
        NULL,
};

r_device const bm5 = {
        .name        = "bm5-v2 12V Battery Monitor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 225,
        .long_width  = 675,
        .reset_limit = 6000,
        .decode_fn   = &bm5_decode,
        .fields      = output_fields,
};
