/** @file
    BM5 v2.0 12V Automotive Wireless Battery Monitor.

    Copyright (C) 2024 Cameron Murphy

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
BM5 v2.0 12V Automotive Battery Monitor.  Sold as "ANCEL BM200" on Amazon, and "QUICKLYNKS BM5-D" on AliExpress

The sensor transmits a single message, with all relevant data about every 10 seconds at 433.92 MHz,

The transmission is inverted from the normal OOK_PULSE_PWM decoder, with a "0" represented as a short pulse of 225us, and a 675us gap,
and a "1" represented as a long 675us pulse, and a 225us gap.  The implementaion below initially inverters the buffer to correct for this.

Each message consists of a preamble (long pulse, plus eight 50% symbol length pulses) sent at double the normal data rate, then a one byte pause (at regular data rate),
then ten bytes of payload, and a one byte CRC.  The preamble is decoded as (0x7F 0x80) by rtl_433 (in the native, non-inverted state) due to the initial pulse.

Payload:

- I = 3 byte ID
- S = 7 bits for battery State of Health (SOH) - 0 to 100 percent
- C = 1 bit flag for charging system error (!CHARGING on display --probably triggered if running voltage below ~13v)
- s = 7 bits for battery State of Charge (SOC) 0 to 100 percent
- c = 1 bit flag for cranking system error. (!CRANKING indicator on display - triggered if starting voltage drops for too long -- excessive cranking)
- T = 8 byte (signed) for sensor temperature (degrees C, converted if necessary in display)
- V = 16 bits, little endian for current battery voltage (Voltage is displayed as a float with 2 significant digits.  The 16 bit int represents this
      voltage, multiplied by 1600.)
- v = 16 bits, little endian for previous low voltage during last start.  (Is probably used for the algorithm to determine battery health.  This value
      will be closer to resting voltage for healthy batteries) Same 1600 muliplier as above.
- R = 1 byte Checksum

    msg:    IIIIIIIIIIIIIIIIIIIIIIIISSSSSSSCssssssscTTTTTTTTVVVVVVVVVVVVVVVVvvvvvvvvvvvvvvvvRRRRRRRR
    ID:24h SOH:7d CRANKING:1b SOC:7d CHARGING:1b TEMP:8s V_CUR:16d V_START:16d CRC:8h

*/

#include "decoder.h"

static int bm5_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[11];

    bitbuffer_invert(bitbuffer); // This device sends data inverted relative to the
                                 // OOK_PWM decoder output.

    if (bitbuffer->num_rows != 1) // Only one message per transmission
        return DECODE_ABORT_EARLY;

    //check correct data length
    if (bitbuffer->bits_per_row[0] != 88) // 10 bytes dayat + 1 byte checksum)
        return DECODE_ABORT_LENGTH;

    bitbuffer_extract_bytes(bitbuffer, 0, 0, b, sizeof(b)*8);

    //check for valid checksum
    if (b[10] != add_bytes(&b[0], 10)){
        return DECODE_FAIL_MIC; // failed checksum - invalid message
    }

    int id   =  b[0] << 16 | b[1] << 8 | b[2];
    int soh  = b[3] >> 1; // State of Health encoded in 1st 7 bits
    int cranking = b[3] & 0x01; // Cranking flag in bit 8 of byte 4
    int soc = b[4] >> 1; // State pf Charge encoded in 1st 7 bits
    int charging = b[4] & 0x01;  // Charging flag in bit 8 of byte 5
    int temp = b[5]; // Temperature in C, signed char in byte 6
    int volt1  = b[7] << 8 | b[6]; // Current voltage
    int volt2 = b[9] << 8 | b[8]; // Previous starting voltage

    float cur_volt = volt1 / 1600.0;   // Convert transmitted values to floats
    float start_volt = volt2 / 1600.0;

    /* clang-format off */
    data = data_make(
            "model",            "",                       DATA_STRING,   "BM5 v2.0",
            "id",               "Device_ID",              DATA_FORMAT,   "%X",          DATA_INT,          id,
            "soh",              "State of Health",        DATA_FORMAT,   "%d %%",       DATA_INT,          soh,
            "cranking",         "Cranking System Error",  DATA_INT,       cranking,
            "soc",              "State of Charge",        DATA_FORMAT,   "%d %%",       DATA_INT,          soc,
            "charging",         "Charging System Error",  DATA_INT,       charging,
            "temperature_C",    "Temperature",            DATA_FORMAT,   "%d C",        DATA_INT,          temp,
            "cur_volt",         "Current Battery Voltage",DATA_DOUBLE,    cur_volt,
            "start_volt",       "Starting Voltage",       DATA_DOUBLE,    start_volt,
            "mic",              "Integrity",              DATA_STRING,   "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "soh",
        "cranking",
        "soc",
        "charging",
        "temperature_C",
        "cur_volt",
        "start_volt",
        "mic",
        NULL,
};

r_device const bm5 = {
        .name        = "BM5 v2.0 12V Battery Monitor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 225,
        .long_width  = 675,
        .reset_limit = 2000,
        .decode_fn   = &bm5_decode,
        .fields      = output_fields,
};