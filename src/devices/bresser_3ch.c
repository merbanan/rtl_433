/** @file
    Bresser sensor protocol.

    Copyright (C) 2015 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Bresser sensor protocol.

The protocol is for the wireless Temperature/Humidity sensor
- Bresser Thermo-/Hygro-Sensor 3CH
  Another sensor sold under the same generic name is handled by
  bresser_st1005h.c.
- also works for Renkforce DM-7511

The sensor sends 15 identical packages of 40 bits each ~60s.
The bits are PWM modulated with On Off Keying.

A short pulse of 250 us followed by a 500 us gap is a 0 bit,
a long pulse of 500 us followed by a 250 us gap is a 1 bit,
there is a sync preamble of pulse, gap, 750 us each, repeated 4 times.
Actual received and demodulated timings might be 2% shorter.

The data is grouped in 5 bytes / 10 nibbles

    [id] [id] [flags] [temp] [temp] [temp] [humi] [humi] [chk] [chk]

- id is an 8 bit random id that is generated when the sensor starts
- flags are 4 bits battery low indicator, test button press and channel
- temp is 12 bit unsigned fahrenheit offset by 90 and scaled by 10
- humi is 8 bit relative humidity percentage
- chk is the sum of the four data bytes
*/
#include "decoder.h"

static int bresser_3ch_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;

    int id, battery_low, channel, temp_raw, humidity;
    // int status, test;
    float temp_f;

    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 40);
    if (r < 0 || bitbuffer->bits_per_row[r] > 42) {
        return DECODE_ABORT_LENGTH;
    }

    b = bitbuffer->bb[r];
    b[0] = ~b[0];
    b[1] = ~b[1];
    b[2] = ~b[2];
    b[3] = ~b[3];
    b[4] = ~b[4];

    if (((b[0] + b[1] + b[2] + b[3] - b[4]) & 0xFF) != 0) {
        decoder_log(decoder, 1, __func__, "checksum error");
        return DECODE_FAIL_MIC;
    }

    id = b[0];
    //status = b[1];
    battery_low = (b[1] & 0x80) >> 7;
    //test = (b[1] & 0x40) >> 6;
    channel = (b[1] & 0x30) >> 4;

    temp_raw = ((b[1] & 0x0F) << 8) + b[2];
    // 12 bits allows for values -90.0 F - 319.6 F (-67 C - 159 C)
    temp_f = (temp_raw - 900) * 0.1f;

    humidity = b[3];

    if ((channel == 0) || (humidity > 100) || (temp_f < -20.0) || (temp_f > 160.0)) {
        decoder_log(decoder, 1, __func__, "data error");
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, "Bresser-3CH",
            "id",            "Id",          DATA_INT,    id,
            "channel",       "Channel",     DATA_INT,    channel,
            "battery_ok",    "Battery",     DATA_INT,    !battery_low,
            "temperature_F", "Temperature", DATA_FORMAT, "%.2f F", DATA_DOUBLE, temp_f,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_F",
        "humidity",
        "mic",
        NULL,
};

r_device const bresser_3ch = {
        .name        = "Bresser Thermo-/Hygro-Sensor 3CH",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 250,  // short pulse is ~250 us
        .long_width  = 500,  // long pulse is ~500 us
        .sync_width  = 750,  // sync pulse is ~750 us
        .gap_limit   = 625,  // long gap (with short pulse) is ~500 us, sync gap is ~750 us
        .reset_limit = 1250, // maximum gap is 1000 us (long gap + longer sync gap on last repeat)
        .decode_fn   = &bresser_3ch_decode,
        .fields      = output_fields,
};

/**
Bresser Thermo-/Hygro-Sensor 3CH, older V0 format.

The sensor sends this format before the V1 format above.
PPM modulated with 475 us pulses, a 0 bit is a 990 us gap, a 1 bit is a 1980 us gap.
8 repeats of 52 bits, separated by a 3975 us gap.

Data layout:

    II F TTT S HH 0000

- I: 8 bit id
- F: 4 bit flags, channel bit 0, channel bit 1, test button, battery low
- T: 12 bit temperature, signed, scale 10, Celsius
- S: 4 bit separator, 0xf
- H: 8 bit humidity
- 0: 16 bit zero tail

There is no integrity check, so two identical repeats are required.

Protocol from the Flipper Zero firmware, `lib/subghz/protocols/bresser_3ch.c`.

Flex decoder:

    rtl_433 -X 'n=bresser_3ch_v0,m=OOK_PPM,s=990,l=1980,g=3000,r=6000,t=300,bits=52,repeats>=2'
*/
static int bresser_3ch_v0_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 2, 52);
    if (r < 0 || bitbuffer->bits_per_row[r] != 52) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t *b = bitbuffer->bb[r];

    // Separator and zero tail
    if ((b[3] >> 4) != 0xf || (b[4] & 0x0f) || b[5] || (b[6] & 0xf0)) {
        return DECODE_FAIL_SANITY;
    }

    int id          = b[0];
    int flags       = b[1] >> 4;
    int channel     = ((flags >> 3) & 1) | (((flags >> 2) & 1) << 1);
    int battery_low = flags & 1;
    int temp_raw    = (int16_t)((((b[1] & 0x0f) << 8) | b[2]) << 4) >> 4; // sign-extend
    int humidity    = ((b[3] & 0x0f) << 4) | (b[4] >> 4);

    // Reject all 0 and all 1 fields
    if (id == 0x00 || id == 0xff || flags == 0x0 || flags == 0xf || temp_raw == -1 || humidity > 100) {
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "Bresser-3CHv0",
            "id",            "Id",          DATA_INT,    id,
            "channel",       "Channel",     DATA_INT,    channel,
            "battery_ok",    "Battery",     DATA_INT,    !battery_low,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_raw * 0.1f,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields_v0[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        NULL,
};

r_device const bresser_3ch_v0 = {
        .name        = "Bresser Thermo-/Hygro-Sensor 3CH, V0 format",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 990,
        .long_width  = 1980,
        .gap_limit   = 3000,
        .reset_limit = 6000,
        .tolerance   = 300,
        .decode_fn   = &bresser_3ch_v0_decode,
        .disabled    = 1, // no integrity check
        .fields      = output_fields_v0,
};
