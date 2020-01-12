/** @file
    Decoder for TFA Drop 30.3233.01.

    Copyright (C) 2020 Michael Haas

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
TFA Drop is a rain gauge with a tipping bucket mechanism.

Links:

 - Product page:
   - https://www.tfa-dostmann.de/en/produkt/wireless-rain-gauge-drop/
 - Manual 2019:
   - https://clientmedia.trade-server.net/1768_tfadost/media/2/66/16266.pdf
 - Manual 2020:
   - https://clientmedia.trade-server.net/1768_tfadost/media/3/04/16304.pdf
 - Discussion of protocol:
   - https://github.com/merbanan/rtl_433/issues/1240

The sensor has part number 30.3233.01. The full package, including the
base station, has part number 47.3005.01.

The device uses PWM encoding:

- 0 is encoded as 250 us pulse and a 500us gap
- 1 is encoded as 500 us pulse and a 250us gap

Note that this encoding scheme is inverted relative to the default
interpretation of short/long pulses in the PWM decoder in rtl_433.
The implementation below thus inverts the buffer. The protocol is
described below in the correct space, i.e. after the buffer has been
inverted.

Not every tip of the bucket triggers a message immediately. In some
cases, artifically tipping the bucket many times lead to the base
station ignoring the signal completely until the device was reset.

Data layout:

```
CCCCIIII IIIIIIII IIIIIIII BCUU XXXX RRRRRRRR CCCCCCCC SSSSSSSS MMMMMMMM
KKKK
```

- C: 4 bit message prefix, always 0x3
- I: 2.5 byte ID
- B: 1 bit, battery_low. 0 if battery OK, 1 if battery is low.
- C: 1 bit, device reset. Set to 1 briefly after battery insert.
- X: Transmission counter
     - Possible values: 0x0, 0x2, 0x4, 0x6, 0x8, 0xA, 0xE, 0xE.
     - Rolls over.
- R: LSB of 16-bit little endian rain counter
- S: MSB of 16-bit little endian rain counter
- C: Fixed to 0xaa
- M: Checksum.
   - Compute with reverse Galois LFSR with byte reflection, generator
     0x31 and key 0xf4.
   - See `compute_checksum` below.
- K: Unknown. Either b1011 or b0111.
     - Distribution: 50:50.


The rain bucket counter represents the number of tips of the rain
bucket. Each tip of the bucket corresponds to 0.254mm of rain.

The rain bucket counter does not start at 0. Instead, the counter
starts at 65526 to indicate 0 tips of the bucket. The counter rolls
over at 65535 to 0, which corresponds to 9 and 10 tips of the bucket.

If no change is detected, the sensor will continue broadcasting
identical values. This lasts at least for 20 minutes,
potentially forever.

The second nibble of byte 3 is a transmission counter: 0x0, 0x2, 0x4,
0x6, 0x8, 0xa, 0xc, 0xe. After the transmission with counter 0xe, the
counter rolls over to 0x0 on the next transmission and the cycle starts
over.

After battery insertion, the sensor will transmit 7 messages in rapid
succession, one message every 3 seconds. After the first message,
the remaining 6 messages have bit 1 of byte 3 set to 1. This could be
some sort of reset indicator.
For these 6 messages, the transmission counter does not increase.

After the full 7 messages, one regular message is sent after 30s.
Afterwards, messages are sent every 45s.
*/

#include "decoder.h"

#define TFA_DROP_BITLEN 66
#define TFA_DROP_STARTBYTE 0x3 /* Inverted already */
#define TFA_DROP_MINREPEATS 5

static uint8_t compute_checksum(uint8_t *msg, int bytes);

static int tfa_drop_30_3233_01_decoder(r_device *decoder,
        bitbuffer_t *bitbuffer)
{
    data_t *data;
    int row_index;
    uint8_t *row_data;

    uint32_t sensor_id;
    uint16_t rain_counter;
    float rain_mm;
    uint8_t battery_low;
    uint8_t observed_checksum;
    uint8_t computed_checksum;

    bitbuffer_invert(bitbuffer);

    row_index = bitbuffer_find_repeated_row(bitbuffer, TFA_DROP_MINREPEATS,
            TFA_DROP_BITLEN);
    if (row_index < 0 || bitbuffer->bits_per_row[row_index] > TFA_DROP_BITLEN + 16) {
        return 0;
    }

    row_data = bitbuffer->bb[row_index];

    /*
     * Reject rows that don't start with the correct start byte.
     */
    if ((row_data[0] & 0xf0) != (TFA_DROP_STARTBYTE << 4)) {
        return 0;
    }

    /*
     * Validate checksum
     */
    observed_checksum = row_data[7];
    computed_checksum = compute_checksum(row_data, 7);
    if (observed_checksum != computed_checksum) {
        return 0;
    }

    /*
     * After validation, start parsing data.
     */

    /*
     * Mask first nibble in row_data[0] it is a constant message prefix.
     */
    sensor_id = (row_data[0] & 0x0f) << 16 | row_data[1] << 8 | row_data[2];

    rain_counter = row_data[6] << 8 | row_data[4];
    rain_counter = rain_counter + 10;

    rain_mm = rain_counter * 0.254;

    battery_low = (row_data[3] & 0x80) >> 7;

    /* clang-format off */
    data = data_make(
            "model", "", DATA_STRING, "TFA-Drop-30.3233.01",
            "id",    "", DATA_FORMAT, "%5x", DATA_INT,  sensor_id,
            "rain_mm", "Rain in MM", DATA_DOUBLE, rain_mm,
            "battery_ok", "Battery OK", DATA_INT,    !battery_low,
            "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "rain_mm",
        "battery_ok",
        "mic",
        NULL,
};

r_device tfa_drop_30_3233_01 = {
        .name        = "TFA Drop Rain Gauge 30.3233.01",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 255,
        .long_width  = 510,
        .gap_limit   = 1300,
        .reset_limit = 2500,
        .sync_width  = 750,
        .decode_fn   = &tfa_drop_30_3233_01_decoder,
        .disabled    = 0,
        .fields      = output_fields,
};

/**
Compute checksum.

The checksum is generated by feeding the first 7 message bytes to a
reverse Galois LFSR with byte reflection, generator 0x31 and key 0xf4.
 
These parameters have been determined with the `revdgst` tool found at
[triq-org/revdgst](https://github.com/triq-org/revdgst/):
 
```
./revdgst codes.txt
[...]
Done with g 31 k f4 final XOR c0 using xor xor (100 %)
Time elapsed in s: 0.38 for: Rev-Galois BYTE_REFLECT
[...]
```

Note that `revdgst` will consider the last byte as the checksum which it
will reverse engineer. For the TFA Drop, the last byte is unknown.
Remove it manually before analysis. The final `codes.txt` file should
have the checksum at byte 7 as the last byte in each line.

This function is an abbreviated version of algo_lfsr_digest8_galois
taken from Christian W. Zuckschwerdt's revdgst project:

- Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>
- GPL V2 or later
- https://github.com/triq-org/revdgst/blob/master/src/revdgst.c

*/
static uint8_t compute_checksum(uint8_t *msg, int bytes)
{

    /* Taps of the LFSR */
    const uint8_t gen = 0x31;
    /* 
     * The key is the internal state of the LFSR.
     * 
     * For every bit observed in the stream of message bits, the LFSR
     * is clocked and assumes a new internal state in the key variable.
     * 
     * The LFSR only produces a pseudo-random number
     * on every iteration. The content of the message is irrelevant
     * for this process.
     * 
     * To compute the message digest from this pseudo-random number,
     * the message is inspect bit by bit. If the bit is one,
     * the current key is xor'd into the checksum.
     * 
     * This process continues until the stream of message bits
     * is exhausted.
     * 
    */
    uint8_t key = 0xf4;

    /* Output variable */
    uint8_t digest = 0;

    /* Process message from last byte to first byte */
    for (int k = bytes - 1; k != -1; k += -1) {
        uint8_t data = msg[k];
        /* Process individual bits of each byte */
        for (int bit = 0; bit != 8; bit += 1) {

            /*
             * If current message bit is 1, XOR the current LFSR key
             * into the digest.
             * 
             */
            if ((data >> bit) & 1) {
                digest ^= key;
            }

            /*
             * Galois LFSR:
             * 
             * Shift the LFSR left (the MSB is dropped) and apply the
             * gen (needs to include the dropped MSB as LSB).
             * 
             * If the output bit is 1, flip (xor) the tap bits in the
             * internal LFSR state by xor'ing with gen.
             * 
             */
            uint8_t output_bit = key & 0x80;
            key                = key << 1;
            if (output_bit) {
                key = key ^ gen;
            }
        }
    }
    return digest;
}
