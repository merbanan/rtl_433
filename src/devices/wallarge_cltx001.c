/** @file
    WallarGe CLTX001 Outdoor Temperature Sensor.

    Copyright (C) 2026 Dennis Kehrig

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int wallarge_cltx001_decode(r_device *decoder, bitbuffer_t *bitbuffer)
WallarGe CLTX001 Outdoor Temperature Sensor.

FCC ID: 2AYIQ-TX100 (https://fcc.report/FCC-ID/2AYIQ-TX100)

Can be purchased individually (https://www.amazon.com/dp/B0CB17H77R/) or
bundled with WallarGe clocks like the CL6007 (https://www.amazon.com/dp/B0D9BNSQCS)
and CL7001 (https://www.amazon.com/dp/B0BYNJW532, http://www.us-wallarge.com/item/3015.html).

## Modulation

HIGH/LOW periods are multiples of 250 µs long.
The following uses `-` for HIGHs and `_` for LOWs lasting 250 µs, respectively.

### 1) Preamble: `-___` or `--___` (single pulse followed by a 750 µs gap)

Sometimes, the initial pulse is too short and gets ignored by rtl_433.
When it does get registered by rtl_433, the following gap exceeds the
configured gap limit of 650 µs, resulting in a row with a single bit.
This prevents the bit from becoming part of the following row,
making that row easier to decode.

### 2) Payload with 0 = `-__` and 1 = `--_` (750 µs per symbol)

rtl_433 interprets this as 1 = `-__` and 0 = `--_`, so we have to invert the data.

Sometimes, the transmitter seems to skip ahead by 250 µs and/or flip a bit.
In such cases, rtl_433 may drop some bits and split the row instead.
These partial rows are currently ignored.

For some reason these issues mainly affect periodic transmissions as opposed to
transmissions that occur immediately after changing the transmitter's channel.

### 3) Separator: `_---___---___` (between each repeated payload)

When the payload ends with `--_`, rtl_433 will see two gaps exceeding the limit:

    ...--__---___---___
              ^^^   ^^^

When the payload ends with `-__`, rtl_433 will see three gaps exceeding the limit:

    ...-___---___---___
        ^^^   ^^^   ^^^

For each of the gaps exceeding the configured limit it will close the current row,
resulting in either one or two empty rows occurring between rows with data.

The payload gets sent five times per transmission.

## Payload encoding: 56 bits / 7 bytes

1. `IIIIIIII` Bits 1 to  8 of a uint16_t sensor ID
2. `IIIIIIII` Bits 9 to 16 of a uint16_t sensor ID
3. `00000000` Always zero, unknown purpose, ignored by clock
4. `BMCCTTTT` Battery status (0 = okay, 1 = low),
              test mode (0 = off, 1 = on),
              2-bit channel ID (0 = A, 1 = B, 2 = C),
              bits 1 to  4 of an "int12_t" temperature reading
5. `TTTTTTTT` Bits 5 to 12 of an "int12_t" temperature reading
6. `PPPPP000` Parity data - even number of set bits in byte N => bit N = 1, else 0.
              The remaining 3 bits are always zero, the clock rejects the signal otherwise.
7. `SSSSSSSS` Checksum, sum of bytes 1-5 (indexes 0-4) modulo 256

### Battery mode

Only reported when the battery is low.

### Test mode

Only reported when turned on (not used by the CLTX001).

The sensor transmits immediately after plugging in a battery or selecting a different
channel, and every 31/33/35 seconds thereafter for channels A/B/C, respectively.

Normally the clock will turn its receiver off after receiving a signal until the
sensor is expected to transmit its next signal.
The test mode overrides this behavior and the clock will keep the receiver on.

### Channel ID

The CLTX001 transmitter and matching clocks only support three channels, but
value 3 = D may also occur while changing the transmitter's channel.

### Temperature reading

12 bit signed integer (two's complement) representing 0.1°C increments in temperature.
Range: -204.8 to 204.7°C.
The clock will show HH.H above 70°C (158°F) and LL.L below -40°C (-40°F).

*/

#define BITS_PER_ROW  56
#define BYTES_PER_ROW 7
#define DATA_BYTES    5

static int wallarge_cltx001_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // This value will be changed to reflect the best row.
    // If it doesn't get changed, we didn't see any row with exactly 56 bits.
    int return_value = DECODE_ABORT_LENGTH;

    // Consider each row in order of appearance
    for (int row_index = 0; row_index < bitbuffer->num_rows; row_index++) {
        // 1) Ignore rows that don't have 56 bits

        if (bitbuffer->bits_per_row[row_index] != BITS_PER_ROW) {
            continue;
        }

        // 2) Invert the data

        // Pointer through which we will access the selected row's bytes
        uint8_t *b = bitbuffer->bb[row_index];

        // Invert just the bits in this row
        for (int i = 0; i < BYTES_PER_ROW; i++) {
            b[i] = ~b[i];
        }

        // 3) Ignore rows with an invalid checksum

        // Sum up the first five bytes
        int checksum = add_bytes(b, DATA_BYTES) & 0xFF;

        if (b[6] != checksum) {
            return_value = DECODE_FAIL_MIC;
            continue;
        }

        // 4) Ignore rows with invalid parity data

        uint8_t parity_byte = b[5];
        int parity_valid    = 1;

        // The last three bits must be 0
        if (parity_byte & 0x07) { // 0x07 = 0b00000111
            parity_valid = 0;
        }
        else {
            // parity_byte's Nth bit should be 1 if the Nth byte has an even number of 1s
            for (int byte_index = 0; byte_index < DATA_BYTES; byte_index++) {
                // parity8() returns 0/1 for even/odd parity, the protocol uses the inverse, so fail if equal
                if (parity8(b[byte_index]) == ((parity_byte >> (7 - byte_index)) & 1)) {
                    parity_valid = 0;
                    break;
                }
            }
        }

        if (!parity_valid) {
            return_value = DECODE_FAIL_MIC;
            continue;
        }

        // 5) Extract the actual data and output it

        // Combine the first two bytes into a single sensor ID
        int sensor_id = ((int)b[0] << 8) | b[1];

        // Extract the low battery and test mode bits
        int battery_low = (b[3] & 0x80) >> 7; // 0x80 = 0b10000000
        int test_mode   = (b[3] & 0x40) >> 6; // 0x40 = 0b01000000

        // Extract the channel ID (0-3 => A-D)
        int channel = ((b[3] & 0x30) >> 4); // 0x30 = 0b00110000

        // The temperature is essentially an int12_t starting half-way in b[3].
        // Sign-extend it by shifting it left an extra 4 bits and casting it as an int16_t.
        int temp_raw = (int16_t)(((b[3] & 0x0F) << 12) | (b[4] << 4)); // 0x0F = 0b00001111
        // The temperature is stored as multiples of 0.1°C. Undo the extra shifting by 4 bits and scale it.
        double temp_c = (temp_raw >> 4) * 0.1f;

        /* clang-format off */
        data_t *data = data_make(
                "model",            "Model",        DATA_STRING,    "WallarGe-CLTX001",
                "id",               "Sensor ID",    DATA_INT,       sensor_id,
                "channel",          "Channel",      DATA_INT,       channel + 1,
                "battery_ok",       "Battery",      DATA_COND,      battery_low, DATA_INT, !battery_low,
                "temperature_C",    "Temperature",  DATA_FORMAT,    "%.1f C", DATA_DOUBLE, temp_c,
                "test",             "Test?",        DATA_COND,      test_mode,   DATA_INT, test_mode,
                "mic",              "Integrity",    DATA_STRING,    "CHECKSUM", // Technically CHECKSUM+PARITY
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);

        // We've found a valid row and will ignore any remaining rows, so 1 is the number of packets successfully decoded
        return_value = 1;
        break;
    }

    return return_value;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "test",
        "mic",
        NULL,
};

r_device const wallarge_cltx001 = {
        .name        = "WallarGe CLTX001 Outdoor Temperature Sensor",
        .modulation  = OOK_PULSE_PWM,
        .tolerance   = 75,
        .short_width = 250,
        .long_width  = 500,
        .gap_limit   = 650, // Gaps that deliniate rows are ~700-750 µs long and tolerance does not apply to the gap limit
        .reset_limit = 1250,
        .decode_fn   = &wallarge_cltx001_decode,
        .fields      = output_fields,
};
