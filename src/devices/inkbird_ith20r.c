/** @file
    Decoder for Inkbird ITH-20R.

    Copyright (C) 2020 Dmitriy Kozyrev

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Decoder for Inkbird ITH-20R.

https://www.ink-bird.com/products-data-logger-ith20r.html

Also: Inkbird IBS-P01R Pool Thermometer.

The compact 3-in-1 multifunction outdoor sensor transmits the data on 433.92 MHz.
The device uses FSK-PCM encoding,
The device sends a transmission every ~80 sec.

Decoding borrowed from https://groups.google.com/forum/#!topic/rtl_433/oeExmwoBI0w

- Total packet length 14563 bits:
- Preamble: aa aa aa ... aa aa (14400 on-off sync bits)
- Sync Word (16 bits): 2DD4
- Data (147 bits):
- Byte    Sample      Comment
- 0-2     D3910F      Always the same across devices, a device type?
- 3       00          00 - normal work , 40 - unlink sensor (button pressed 5s), 80 - battery replaced
- 4       01          Changes from 1 to 2 if external sensor present
- 5-6     0301        Unknown (also seen 0201), sw version? Seen 0x0001 on IBS-P01R.
- 7       58          Battery % 0-100
- 8-9     A221        Device id, always the same for a sensor but each sensor is different
- 10-11   D600        Temperature in C * 10, little endian, so 0xD200 is 210, 21.0C or 69.8F
- 12-13   F400        Temperature C * 10 for the external sensor,  0x1405 if not connected
- 14-15   D301        Relative humidity %  * 10, little endian, so 0xC501 is 453 or 45.3%
- 16-17   38FB        CRC16
- 18      0           Unknown 3 bits (seen 0 and 2)

CRC16 (bytes 0-15), without sync word):
poly=0x8005  init=0x2f61  refin=true  refout=true  xorout=0x0000  check=0x3583  residue=0x0000

To look at unknown data fields run with -vv key.

Decoder written by Dmitriy Kozyrev, 2020
*/

#include "decoder.h"

#define INKBIRD_ITH20R_CRC_POLY 0xA001  // reflected 0x8005
#define INKBIRD_ITH20R_CRC_INIT 0x86F4  // reflected 0x2f61


static int inkbird_ith20r_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4};

    data_t *data;
    uint8_t msg[19];

    if ((bitbuffer->num_rows != 1)
            || (bitbuffer->bits_per_row[0] < 187)
            /*|| (bitbuffer->bits_per_row[0] > 14563)*/) {
        decoder_logf(decoder, 2, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH; // Unrecognized data
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (start_pos == bitbuffer->bits_per_row[0]) {
        return DECODE_FAIL_SANITY;  // Not found preamble
    }

    start_pos += sizeof (preamble_pattern) * 8;
    unsigned len = bitbuffer->bits_per_row[0] - start_pos;

    decoder_logf(decoder, 2, __func__, "start_pos=%u len=%u", start_pos, len);

    if (((len + 7) / 8) < sizeof (msg)) {
        decoder_logf(decoder, 1, __func__, "%u too short", len);
        return DECODE_ABORT_LENGTH; // Message too short
    }
    // truncate any excessive bits
    len = MIN(len, sizeof (msg) * 8);

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, len);

    // CRC check
    uint16_t crc_calculated = crc16lsb(msg, 16, INKBIRD_ITH20R_CRC_POLY, INKBIRD_ITH20R_CRC_INIT);
    uint16_t crc_received = msg[17] << 8 | msg[16];

    decoder_logf(decoder, 2, __func__, "CRC 0x%04X = 0x%04X", crc_calculated, crc_received);

    if (crc_received != crc_calculated) {
        decoder_logf(decoder, 1, __func__, "CRC check failed (0x%04X != 0x%04X)", crc_calculated, crc_received);
        return DECODE_FAIL_MIC;
    }

    uint32_t subtype = (msg[3] << 24 | msg[2] << 16 | msg[1] << 8 | msg[0]);
    int sensor_num = msg[4];
    uint16_t word56 = (msg[6] << 8 | msg[5]);
    float battery_ok = msg[7] * 0.01f;
    uint16_t sensor_id = (msg[9] << 8 | msg[8]);
    float temperature = ((int16_t)(msg[11] << 8 | msg[10])) * 0.1f;
    float temperature_ext = ((int16_t)(msg[13] << 8 | msg[12])) * 0.1f;
    float humidity = (msg[15] << 8 | msg[14]) * 0.1f;
    uint8_t word18 = msg[18];

    decoder_logf(decoder, 1, __func__, "dword0-3= 0x%08X word5-6= 0x%04X byte18= 0x%02X", subtype, word56, word18);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Inkbird-ITH20R",
            "id",               "",             DATA_INT,    sensor_id,
            "battery_ok",       "Battery",      DATA_DOUBLE, battery_ok,
            "sensor_num",       "",             DATA_INT,    sensor_num,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "temperature_2_C",  "Temperature2", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature_ext,
            "humidity",         "Humidity",     DATA_FORMAT, "%.1f %%", DATA_DOUBLE, humidity,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "sensor_num",
        "temperature_C",
        "temperature_2_C",
        "humidity",
        "mic",
        NULL,
};

r_device const inkbird_ith20r = {
        .name        = "Inkbird ITH-20R temperature humidity sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,  // Width of a '0' gap
        .long_width  = 100,  // Width of a '1' gap
        .reset_limit = 4000, // Maximum gap size before End Of Message [us]
        .decode_fn   = &inkbird_ith20r_callback,
        .fields      = output_fields,
};
