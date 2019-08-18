/** @file
    Conrad Electronics S3318P outdoor sensor.

    Copyright (C) 2016 Martin Hauke
    Enhanced (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
*/
/**
Largely the same as esperanza_ews, kedsum.
\sa esperanza_ews.c kedsum.c

Transmit Interval: every ~50s.
Message Format: 40 bits (10 nibbles).

    Byte:      0        1        2        3        4
    Nibble:    1   2    3   4    5   6    7   8    9   10
    Type:   00 IIIIIIII ??CCTTTT TTTTTTTT HHHHHHHH WB??XXXX

- 0: Preamble
- I: sensor ID (changes on battery change)
- C: channel number
- T: temperature
- H: humidity
- W: tx-button pressed
- B: low battery
- ?: unknown meaning
- X: CRC-4 poly 0x3 init 0x0 xor last 4 bits

Example data:

    [01] {42} 04 15 66 e2 a1 00 : 00000100 00010101 01100110 11100010 10100001 00 ---> Temp/Hum/Ch:23.2/46/1

Temperature:
- Sensor sends data in °F, lowest supported value is -90°F
- 12 bit unsigned and scaled by 10 (Nibbles: 6,5,4)
- in this case "011001100101" =  1637/10 - 90 = 73.7 °F (23.17 °C)

Humidity:
- 8 bit unsigned (Nibbles 8,7)
- in this case "00101110" = 46

Channel number: (Bits 10,11) + 1
- in this case "00" --> "00" +1 = Channel1

Battery status: (Bit 33) (0 normal, 1 voltage is below ~2.7 V)
- TX-Button: (Bit 32) (0 indicates regular transmission, 1 indicates requested by pushbutton)

Random Code / Device ID: (Nibble 1)
- changes on every battery change
*/

#include "decoder.h"

static int s3318p_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    uint8_t b[5];
    data_t *data;

    // ignore if two leading sync pulses (Esperanza EWS)
    if (bitbuffer->bits_per_row[0] == 0 && bitbuffer->bits_per_row[1] == 0)
        return 0;

    // the signal should have 6 repeats with a sync pulse between
    // require at least 4 received repeats
    int r = bitbuffer_find_repeated_row(bitbuffer, 4, 42);
    if (r < 0 || bitbuffer->bits_per_row[r] != 42)
        return 0;

    // remove the two leading 0-bits and align the data
    bitbuffer_extract_bytes(bitbuffer, r, 2, b, 40);

    // CRC-4 poly 0x3, init 0x0 over 32 bits then XOR the next 4 bits
    int crc = crc4(b, 4, 0x3, 0x0) ^ (b[4] >> 4);
    if (crc != (b[4] & 0xf))
        return 0;

    int id          = b[0];
    int channel     = ((b[1] & 0x30) >> 4) + 1;
    int temp_raw    = ((b[2] & 0x0f) << 8) | (b[2] & 0xf0) | (b[1] & 0x0f);
    float temp_f    = (temp_raw - 900) * 0.1;
    int humidity    = ((b[3] & 0x0f) << 4) | ((b[3] & 0xf0) >> 4);
    int button      = b[4] >> 7;
    int battery_low = (b[4] & 0x40) >> 6;

    data = data_make(
            "model",            "",             DATA_STRING, _X("Conrad-S3318P","S3318P Temperature & Humidity Sensor"),
            "id",               "ID",           DATA_INT, id,
            "channel",          "Channel",      DATA_INT, channel,
            "battery",          "Battery",      DATA_STRING, battery_low ? "LOW" : "OK",
            "button",           "Button",       DATA_INT, button,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.02f F", DATA_DOUBLE, temp_f,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "button",
    "temperature_F",
    "humidity",
    "mic",
    NULL
};

r_device s3318p = {
    .name           = "Conrad S3318P Temperature & Humidity Sensor",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 1900,
    .long_width     = 3800,
    .gap_limit      = 4400,
    .reset_limit    = 9400,
    .decode_fn      = &s3318p_callback,
    .disabled       = 0,
    .fields         = output_fields
};
