/** @file
    Shenzhen Wale WL-TH6R Temperature & Humidity Sensor.

    Copyright (C) 2026 Dennis Kehrig

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int shenzhen_wale_wl_th6r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Shenzhen Wale WL-TH6R Temperature & Humidity Sensor.

FCC ID: 2A2X7-WL-TH6R

Typically bundled with Wi-Fi base stations powered by Tuya like the WL-TH16-R.

AliExpress seller SMATRUL uses these related product IDs:
- WSD023-WIF-433-W12 for a WL-TH16-R style base station (https://www.aliexpress.us/item/3256808493034721.html).
- WSD024-W-433 for the sensors (bundled with the base station)

AliExpress seller ONENUO also sells similar and identical looking sensors. For example, this appears to be the
same base station, but with slightly different sensors: https://www.aliexpress.us/item/3256810348959477.html.
These sensors are also bundled with this product: https://www.aliexpress.us/item/3256810400966935.html.

According to user avg-I on GitHub, this decoder works with those sensors as well.
He discovered this manual for the WL-TH6R: https://manuals.plus/m/851951e2e953a4eeb0524095efa5695751c6d04759095ea022b4db18bb58d9f4

Shenzhen Wale may be the OEM for these other brands.

## Modulation

PWM with the following timings:

| Type | Pulse  | Gap     | Total   | Notes                                                        |
| ---- | ------ | ------- | ------- | ------------------------------------------------------------ |
| 0    | 365 µs |  605 µs |  970 µs | Interpreted as 1 due to short = 1 convention                 |
| 1    | 605 µs |  365 µs |  970 µs | Interpreted as 0 due to long  = 0 convention                 |
| STOP | 275 µs | 3200 µs | 3475 µs | Typically ignored due to tolerance = 50 and 275 < (365 - 50) |

## Frame Structure

1. Preamble: 01010101 + STOP
2. Payload: 72 bits + STOP, five times in a row

## Payload Structure

Nine bytes with whitening:

1. `DDDDDDDWM` (D = data byte, W = whitening byte, M = MIC byte). Ds need to be XOR'd with W.<br>
2. `IIITTHBCM` (I = ID byte, T = temperature byte, H = humidity byte, B = battery byte, C = pairing bit + cycle, M = MIC byte)

- b[0], b[1], b[2]: 24-bit sensor ID
- b[3], b[4]: 16-bit temperature value (int16_t, multiples of 0.1°C)
- b[5]: Relative humidity in percentage points. Base station rejects transmissions with b[5] > 127.
- b[6]: Battery level in percentage points. 0% = ~2.3V, 100% = ~3V. Base station accepts any value including 255%.
- b[7]: `PCCCCCCC` (P = 1 for pairing mode, 0 otherwise. The other bits make up a counter from 0-64 incremented each measurement cycle.)
- b[8]: MIC value = 0xA5 ^ xor(b[1..7]) ^ (sum(b[1..7]) & 0xFF) ^ (sum(b[1..7]) >> 8)

## Notes

The MIC algorithm isn't robust enough that a single row with a valid MIC value can be trusted.
Samples with a valid MIC value, but corrupted sensor IDs have been observed.
Therefore only rows that occur at least twice will be reported.

*/

#define BITS_PER_ROW       72
#define BYTES_PER_ROW      9
#define DATA_BYTES_PER_ROW 7

static int shenzhen_wale_wl_th6r_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Pick the first row with at least 72 bits that occurs at least twice (ignoring additional bits)
    int row = bitbuffer_find_repeated_prefix(bitbuffer, 2, BITS_PER_ROW);

    if (row < 0) {
        decoder_logf(decoder, 2, __func__, "No row with at least 72 bits occurred at least twice");
        return DECODE_ABORT_EARLY;
    }

    if (bitbuffer->bits_per_row[row] > BITS_PER_ROW + 1) {
        decoder_logf(decoder, 2, __func__, "Row index %d has too many bits (%u)", row, bitbuffer->bits_per_row[row]);
        return DECODE_ABORT_LENGTH;
    }

    // Pointer through which we will access the bytes in the selected row
    uint8_t *b = bitbuffer->bb[row];

    // Invert just the bits in this row
    for (int i = 0; i < BYTES_PER_ROW; i++) {
        b[i] = ~b[i];
    }

    // Dewhiten the data
    for (int i = 0; i < 7; i++) {
        b[i] ^= b[7];
    }

    // Calculate the MIC value (made possible by a >7h run of Claude Opus 4.6 to identify the algorithm)
    int xor = xor_bytes(b, DATA_BYTES_PER_ROW);
    int sum = add_bytes(b, DATA_BYTES_PER_ROW);
    int mic = 0xA5 ^ xor ^ (sum & 0xFF) ^ (sum >> 8); // 0xA5 = 1010 0101

    // Ignore rows with an invalid MIC value
    if (b[8] != mic) {
        decoder_logf(decoder, 2, __func__, "Row index %d has MIC %u, expected %u", row, b[8], mic);
        return DECODE_FAIL_MIC;
    }

    // The temperature is an int16_t starting in b[3], use a cast to sign-extend
    int temp_raw = (int16_t)((b[3] << 8) | b[4]);
    // The temperature is stored as multiples of 0.1°C
    float temp_c = temp_raw * 0.1f;
    // The sensor's specified temperature range is -20°C to 60°C. The base station accepts more extreme values, the cloud API does not.
    // Note that the sensors actually do report values > 60°C.
    if (temp_c < -20.0f || temp_c > 60.0f) {
        decoder_logf(decoder, 2, __func__, "Row index %d has implausible temperature: %.1f C", row, temp_c);
        return DECODE_FAIL_SANITY;
    }

    // Extract the relative humidity (percentage)
    int humidity_pct = b[5];
    // The base station rejects transmissions with humidity values > 127%. Base station and API will display at most 99%.
    if (humidity_pct > 127) {
        decoder_logf(decoder, 2, __func__, "Row index %d has implausible humidity: %u%%", row, humidity_pct);
        return DECODE_FAIL_SANITY;
    }

    // Combine b[0], b[1] and b[2] into a single sensor ID.
    // In hex notation this matches what the base station reports via the Tuya API.
    int sensor_id = ((int)b[0] << 16) | ((int)b[1] << 8) | b[2];

    // Extract the battery level (percentage, can be 0% = ~2.3V, can be > 100% = ~3.0V).
    // The base station accepts transmissions with 255% battery level, the cloud API caps the returned value at 100%.
    int battery_pct = b[6];

    // Extract the pairing mode bit
    int pairing = b[7] >> 7;
    /*
    Extract the cycle counter. Every ~10s the sensor takes a measurement and increments the counter, but only
    transmits when there's enough of a change (e.g. +/- 0.3°C or +/- 3% RH) or more than five minutes have passed.
    A good way to test the counter behavior is to press the pairing button, which causes the sensor to send
    13 transmissions in a row, incrementing the counter each time.
    Normally the counter goes from 0-64 (0x40) and then starts over, but there's some odd behavior for the
    first transmission after inserting batteries where the bits for the counter might be uninitialized,
    seemingly reusing memory used for the battery level. When inserting sufficiently fresh batteries this could
    lead to counter values > 64, so let's cap the value to something inside the expected range just in case.
    */
    int cycle = (b[7] & 0x40) ? 0x40 : (b[7] & 0x3F);

    decoder_logf(decoder, 2, __func__, "Row index %d is valid", row);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "Model",            DATA_STRING,    "WL-TH6R",
            "id",               "Sensor ID",        DATA_FORMAT,    "%06X", DATA_INT, sensor_id,
            "battery_ok",       "Battery",          DATA_COND,      battery_pct < 20, DATA_INT, 0, // The mobile app sends a low battery notification below 20%
            "battery_pct",      "Battery level",    DATA_FORMAT,    "%d %%", DATA_INT, battery_pct, // Note: this might change with #3103
            "temperature_C",    "Temperature",      DATA_FORMAT,    "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",         DATA_FORMAT,    "%d %%", DATA_INT, humidity_pct,
            "pairing",          "Pairing?",         DATA_COND,      pairing, DATA_INT, pairing,
            "cycle",            "Cycle",            DATA_INT,       cycle,
            "mic",              "Integrity",        DATA_STRING,    "CHECKSUM", // With a very loose definition of "checksum"
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "battery_pct",
        "temperature_C",
        "humidity",
        "pairing",
        "cycle",
        "mic",
        NULL,
};

r_device const shenzhen_wale_wl_th6r = {
        .name        = "Shenzhen Wale WL-TH6R Temperature & Humidity Sensor",
        .modulation  = OOK_PULSE_PWM,
        .tolerance   = 50,
        .short_width = 365,
        .long_width  = 605,
        .gap_limit   = 780,
        .reset_limit = 4000,
        .decode_fn   = &shenzhen_wale_wl_th6r_decode,
        .fields      = output_fields,
};
