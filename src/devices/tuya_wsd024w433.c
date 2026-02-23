/** @file
    Tuya WSD024-W-433 Temperature & Humidity Sensor.

    Copyright (C) 2026 Dennis Kehrig

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define BITS_PER_ROW       72
#define BYTES_PER_ROW      9
#define DATA_BYTES_PER_ROW 7
#define MAX_CANDIDATES     4

/**
Tuya WSD024-W-433 Temperature & Humidity Sensor.

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

2. `DDDDDDDWM` (D = data byte, W = whitening byte, M = MIC byte). Ds need to be XOR'd with W.<br>
3. `IIITTHBCM` (I = ID byte, T = temperature byte, H = humidity byte, B = battery byte, C = pairing bit + cycle, M = MIC byte)

- b[0], b[1], b[2]: 24-bit sensor ID
- b[3], b[4]: 16-bit temperature value (int16_t, multiples of 0.1°C)
- b[5]: Relative humidity in percentage points. Base station rejects transmissions with b[5] > 127.
- b[6]: Battery level in percentage points. 0% = ~2.3V, 100% = ~3V. Base station accepts any value including 255%.
- b[7]: `PCCCCCCC` (P = 1 for pairing mode, 0 otherwise. The other bits make up a counter from 0-64 incremented each measurement cycle.)
- b[8]: MIC value = 0xA5 ^ xor(b[1..7]) ^ (sum(b[1..7]) & 0xFF) ^ (sum(b[1..7]) >> 8)

## Notes

Because these sensors come in sets of up to 10, and may transmit every ~10 seconds (or every 400 ms in
paring mode), we can easily encounter rows from more than one sensor in the same decoder call.
As a result it's best not to just ignore additional rows after finding one that is valid.

The MIC algorithm also isn't robust enough that a single row with a valid MIC value can be trusted.
Samples with a valid MIC value, but corrupted sensor IDs have been observed.

This decoder will report all payloads with a valid MIC value, but also report the number of rows in the same
transmission that had the same data. This way downstream consumers can filter out rows with a count of 1 if
they want to use redundancy as an additional integrity check.

*/
static int tuya_wsd024w433_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // To support reporting data from multiple sensor IDs in the same decoder call and to support reporting how
    // often each reported row occurred, we first collect indexes of unique rows with the right amount of bits,
    // and count how often they occur.
    // In the most common scenario we will see five rows with 72 bits. The first row will lead to a new entry
    // in both candidate_rows and candidate_counts and the remaining four rows will be compared against
    // that first and only entry, keeping the complexity low.
    // Across more than 20,000 recordings from eight sensors, just a few contained three unique rows with enough
    // bits, and none had more. Therefore, allowing up to four candidates is probably good enough in practice.
    // In principle it would be nice to immediately discard rows with invalid MIC values, but computing the MIC value
    // requires altering the row's data, complicating the detection of duplicates. So we accept the possibility
    // that unique corrupted rows fill up the candidate arrays and prevent later valid rows from being considered.
    // Typically when there are reception issues we got lots of rows with less than 72 bits instead,
    // which are easy to ignore.

    int num_candidates = 0;
    unsigned candidate_rows[MAX_CANDIDATES];
    unsigned candidate_counts[MAX_CANDIDATES];

    for (int row_index = 0; row_index < bitbuffer->num_rows; row_index++) {
        // Ignore rows that don't have 72 or 73 bits.
        // Each row should end with a ~275 µs long extra pulse which is normally ignored due to being too short,
        // but it could potentially be long enough on occasion to make up a 73rd bit.
        if (bitbuffer->bits_per_row[row_index] != BITS_PER_ROW && bitbuffer->bits_per_row[row_index] != BITS_PER_ROW + 1) {
            continue;
        }

        // Check if this row matches any of the candidates we've already collected.
        int matches = 0; // Could be a bool, but we don't like those around here.
        for (int candidate_index = 0; candidate_index < num_candidates; candidate_index++) {
            // We only compare the first 72 bits since the potential 73th bit doesn't matter
            if (bitbuffer_compare_rows(bitbuffer, candidate_rows[candidate_index], row_index, BITS_PER_ROW)) {
                // Increment matches so we know this row matches an existing candidate
                matches++;
                // Increment the matching candidate's count so we know how often it occurred
                candidate_counts[candidate_index]++;
                // If this row does match a candidate it shouldn't also match other candidates, so stop here
                break;
            }
        }

        // If this row didn't match an existing candidate
        if (!matches) {
            // If we have room for another candidate, add the current row as one
            if (num_candidates < MAX_CANDIDATES) {
                candidate_rows[num_candidates]   = row_index;
                candidate_counts[num_candidates] = 1;
                num_candidates++;
            }
            else {
                // Note: Would show with -vv
                decoder_logf(decoder, 1, __func__, "Unable to add more candidates (max: %u)", MAX_CANDIDATES);
                // We can't stop here since later rows might be duplicates of existing candidates and need to be counted
            }
        }
    }

    int successes       = 0;
    int mic_failures    = 0;
    int sanity_failures = 0;

    for (int candidate_index = 0; candidate_index < num_candidates; candidate_index++) {
        // Pointer through which we will access the bytes in the candidate's row
        uint8_t *b = bitbuffer->bb[candidate_rows[candidate_index]];

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
            decoder_logf(decoder, 2, __func__, "#%u has MIC %u, expected %u", candidate_index + 1, b[8], mic);
            mic_failures++;
            continue;
        }

        // The temperature is an int16_t starting in b[3], use a cast to sign-extend
        int temp_raw = (int16_t)((b[3] << 8) | b[4]);
        // The temperature is stored as multiples of 0.1°C
        double temp_c = temp_raw * 0.1f;
        // The sensor's specified temperature range is -20°C to 60°C. The base station accepts more extreme values, the cloud API does not.
        // Note that the sensors actually do report values > 60°C.
        if (temp_c < -20.0f || temp_c > 60.0f) {
            decoder_logf(decoder, 2, __func__, "#%u has implausible temperature: %.1f C", candidate_index + 1, temp_c);
            sanity_failures++;
            continue;
        }

        // Extract the relative humidity (percentage)
        int humidity_pct = b[5];
        // The base station rejects transmissions with humidity values > 127%. Base station and API will display at most 99%.
        if (humidity_pct > 127) {
            decoder_logf(decoder, 2, __func__, "#%u has implausible humidity: %u%%", candidate_index + 1, humidity_pct);
            sanity_failures++;
            continue;
        }

        // Combine b[7], b[1] and b[2] into a single sensor ID.
        // In hex notation this matches what the base station reports via the Tuya API.
        int sensor_id = ((int)b[0] << 16) | ((int)b[1] << 8) | b[2];

        // Extract the battery level (percentage, can be 0% = ~2.3V, can be > 100% = ~3.0V).
        // The base station accepts transmissions with 255% battery level, the cloud API caps the returned value at 100%.
        int battery_pct = b[6];

        // Extract the pairing mode bit
        int pairing = b[7] >> 7;
        // Extract the cycle counter. Every ~10s the sensor takes a measurement and increments the counter, but only
        // transmits when there's enough of a change (e.g. +/- 0.3°C or +/- 3% RH) or more than five minutes have passed.
        // A good way to test the counter behavior is to press the pairing button, which causes the sensor to send
        // 13 transmissions in a row, incrementing the counter each time.
        // Normally the counter goes from 0-64 (0x40) and then starts over, but there's some odd behavior for the
        // first transmission after inserting batteries where the bits for the counter might be uninitialized,
        // seemingly reusing memory used for the battery level. When inserting sufficiently fresh batteries this could
        // lead to counter values > 64, so let's cap the value to something inside the expected range just in case.
        int cycle = (b[7] & 0x40) ? 0x40 : (b[7] & 0x3F);

        decoder_logf(decoder, 2, __func__, "#%u is valid", candidate_index + 1);
        successes++;

        /* clang-format off */
        data_t *data = data_make(
                "model",            "Model",            DATA_STRING,    "Tuya-WSD024W433",
                "id",               "Sensor ID",        DATA_FORMAT,    "%06X", DATA_INT, sensor_id,
                "battery_ok",       "Battery",          DATA_COND,      battery_pct < 20, DATA_INT, 0, // The mobile app sends a low battery notification below 20%
                "battery_pct",      "Battery level",    DATA_FORMAT,    "%d %%", DATA_INT, battery_pct, // Note: this might change with #3103
                "temperature_C",    "Temperature",      DATA_FORMAT,    "%.1f C", DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",         DATA_FORMAT,    "%d %%", DATA_INT, humidity_pct,
                "pairing",          "Pairing?",         DATA_COND,      pairing, DATA_INT, pairing,
                "cycle",            "Cycle",            DATA_INT,       cycle,
                "count",            "Count",            DATA_FORMAT,    "%ux", DATA_INT, candidate_counts[candidate_index],
                "mic",              "Integrity",        DATA_STRING,    "CHECKSUM", // With a very loose definition of "checksum"
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
    }

    int return_value;
    if (successes) {
        return_value = successes;
    }
    else if (sanity_failures) {
        return_value = DECODE_FAIL_SANITY;
    }
    else if (mic_failures) {
        return_value = DECODE_FAIL_MIC;
    }
    else {
        return_value = DECODE_ABORT_LENGTH;
        decoder_logf(decoder, 2, __func__, "No rows had 72 or 73 bits");
    }

    decoder_logf(decoder, 2, __func__, "Return value: %d", return_value);
    return return_value;
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
        "count",
        "mic",
        NULL,
};

r_device const tuya_wsd024w433 = {
        .name        = "Tuya WSD024-W-433 Temperature & Humidity Sensor",
        .modulation  = OOK_PULSE_PWM,
        .tolerance   = 50,
        .short_width = 365,
        .long_width  = 605,
        .gap_limit   = 780,
        .reset_limit = 4000,
        .decode_fn   = &tuya_wsd024w433_decode,
        .fields      = output_fields,
};
