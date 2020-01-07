/** @file
    Template decoder for TFA Drop 30.3233.01.

    Copyright (C) 2020 Michael Haas

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
 
TFA Drop is a rain gauge with a tipping bucket mechanism.

Links:
 - Product page: https://www.tfa-dostmann.de/en/produkt/wireless-rain-gauge-drop/
 - Manual 2019: https://clientmedia.trade-server.net/1768_tfadost/media/2/66/16266.pdf
 - Manual 2020: https://clientmedia.trade-server.net/1768_tfadost/media/3/04/16304.pdf

The sensor has part number 30.3233.01. The full package, including the base
station, has part number 47.3005.01.

The device uses PWM encoding:

- 0 is encoded as 250 us pulse and a 500us gap
- 1 is encoded as 500 us pulse and a 250us gap

Note that this encoding scheme is inverted relative to the default
interpretation of short/long pulses in the PWM decoder in rtl_433.
The implementation below thus inverts the buffer. The protocol is
described below in the correct space, i.e. after the buffer has
been inverted.

The device sends six identical messages.

Not every tip of the bucket triggers a message immediately. In some cases,
artifically tipping the bucket many times lead to the base station ignoring
the signal completely until the device was reset.

Data layout:

```
CCCCIIII IIIIIIII IIIIIIII BUUUUUUUU RRRRRRRR UUUUUUUU SSSSSSSS UUUUUUUU UUUUUUUU UUUU
```

- C: 4 bit message prefix, always 0x3
- I: 2.5 byte ID
- B: 1 bit, battery_low. 0 if battery OK, 1 if battery is low.
- R: LSB of 16-bit little endian rain counter
- S: MSB of 16-bit little endian rain counter
- U: Unknown

The rain bucket counter represents the number of tips of the rain bucket. Each tip of the
bucket corresponds to 0.254mm of rain.

The rain bucket counter does not start at 0. Instead, the counter starts at 65526
to indicate 0 tips of the bucket. The counter rolls over at 65535 to 0, which corresponds
to 9 and 10 tips of the bucket, respectively.

*/

#include "decoder.h"

/*
 * Message is 66 bits long
 * Messages start with 0x3
 * The message is repeated as 6 packets,
 * require at least 5 repeated packets.
 *
 */
#define TFA_DROP_BITLEN 66
#define TFA_DROP_STARTBYTE 0x3 /* Inverted already */
#define TFA_DROP_MINREPEATS 5

static int tfa_drop_30_3233_01_decoder(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int r;      // a row index
    uint8_t *b; // bits of a row
    uint32_t sensor_id;
    uint16_t rain_counter;
    float rain_mm;
    uint8_t battery_low;

    bitbuffer_invert(bitbuffer);

    /*
     * Or, if you expect repeated packets
     * find a suitable row:
     */

    r = bitbuffer_find_repeated_row(bitbuffer, TFA_DROP_MINREPEATS, TFA_DROP_BITLEN);
    if (r < 0 || bitbuffer->bits_per_row[r] > TFA_DROP_BITLEN + 16) {
        return 0;
    }

    b = bitbuffer->bb[r];

    /*
     * Either reject rows that don't start with the correct start byte:
     * Example message should start with 0xAA
     */
    if ((b[0] & 0xf0) != (TFA_DROP_STARTBYTE << 4)) {
        return 0;
    }

    /*
     * Now that message "envelope" has been validated,
     * start parsing data.
     */
    /* Mask first nibble in b[0] it is a constant message prefix. */
    sensor_id = (b[0] & 0x0f) << 16 | b[1] << 8 | b[2];

    rain_counter = b[6] << 8 | b[4];
    rain_counter = rain_counter + 10;

    rain_mm = rain_counter * 0.254;

    battery_low = (b[3] & 0x80) >> 7;

    /* clang-format off */
    data = data_make(
            "model", "", DATA_STRING, "TFA-Drop-30.3233.01",
            "id",    "", DATA_FORMAT, "%5x", DATA_INT,  sensor_id,
            "rain_mm", "Rain in MM", DATA_DOUBLE, rain_mm,
            "battery_ok", "Battery OK", DATA_INT,    !battery_low,

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
        .disabled    = 1,
        .fields      = output_fields,
};
