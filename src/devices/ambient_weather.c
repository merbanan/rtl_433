/** @file
    Ambient Weather F007TH Thermo-Hygrometer.

    contributed by David Ediger
    discovered by Ron C. Lewis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Ambient Weather F007TH Thermo-Hygrometer.

The check is an LFSR Digest-8, gen 0x98, key 0x3e, init 0x64
*/

#include "decoder.h"

static int ambient_weather_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[6];
    int deviceID;
    int isBatteryLow;
    int channel;
    float temperature;
    int humidity;
    data_t *data;

    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, 6*8);

    uint8_t expected = b[5];
    uint8_t calculated = lfsr_digest8(b, 5, 0x98, 0x3e) ^ 0x64;

    if (expected != calculated) {
        if (decoder->verbose) {
            fprintf(stderr, "Checksum error in Ambient Weather message.    Expected: %02x    Calculated: %02x\n", expected, calculated);
            fprintf(stderr, "Message: ");
            bitrow_print(b, 48);
        }
        return DECODE_FAIL_MIC;
    }

    deviceID = b[1];
    isBatteryLow = (b[2] & 0x80) != 0; // if not zero, battery is low
    channel = ((b[2] & 0x70) >> 4) + 1;
    int temp_f = ((b[2] & 0x0f) << 8) | b[3];
    temperature = (temp_f - 400) * 0.1f;
    humidity = b[4];

    data = data_make(
            "model",          "",             DATA_STRING, _X("Ambientweather-F007TH","Ambient Weather F007TH Thermo-Hygrometer"),
            _X("id","device"),         "House Code",   DATA_INT,    deviceID,
            "channel",        "Channel",      DATA_INT,    channel,
            "battery",        "Battery",      DATA_STRING, isBatteryLow ? "Low" : "OK",
            "temperature_F",  "Temperature",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature,
            "humidity",       "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",            "Integrity",    DATA_STRING, "CRC",
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

/**
Ambient Weather F007TH Thermo-Hygrometer.
@sa ambient_weather_decode()
*/
static int ambient_weather_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // three repeats without gap
    // full preamble is 0x00145 (the last bits might not be fixed, e.g. 0x00146)
    // and on decoding also 0xffd45
    uint8_t const preamble_pattern[2]  = {0x01, 0x45}; // 12 bits
    uint8_t const preamble_inverted[2] = {0xfd, 0x45}; // 12 bits

    int row;
    unsigned bitpos;
    int ret = 0;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_pattern, 12)) + 8+6*8 <=
                bitbuffer->bits_per_row[row]) {
            ret = ambient_weather_decode(decoder, bitbuffer, row, bitpos + 8);
            if (ret > 0) return ret; // for now, break after first successful message
            bitpos += 16;
        }
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_inverted, 12)) + 8+6*8 <=
                bitbuffer->bits_per_row[row]) {
            ret = ambient_weather_decode(decoder, bitbuffer, row, bitpos + 8);
            if (ret > 0) return ret; // for now, break after first successful message
            bitpos += 15;
        }
    }

    return ret;
}

static char *output_fields[] = {
        "model",
        "device", // TODO: delete this
        "id",
        "channel",
        "battery",
        "temperature_F",
        "humidity",
        "mic",
        NULL,
};

r_device ambient_weather = {
        .name        = "Ambient Weather Temperature Sensor",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 500,
        .long_width  = 0, // not used
        .reset_limit = 2400,
        .decode_fn   = &ambient_weather_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
