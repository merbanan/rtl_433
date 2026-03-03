/** @file
    Nexus temperature and optional humidity sensor protocol.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/

#include "decoder.h"

/**
Nexus sensor protocol with ID, temperature and optional humidity.

also FreeTec (Pearl) NC-7345 sensors for FreeTec Weatherstation NC-7344,
also infactory/FreeTec (Pearl) NX-3980 sensors for infactory/FreeTec NX-3974 station,
also Solight TE82S sensors for Solight TE76/TE82/TE83/TE84 stations,
also TFA 30.3209.02 temperature/humidity sensor,
also Unmarked sensor form Rossmann Poland, board markings XS1043 REV02.

The sensor sends 36 bits 12 times,
the packets are ppm modulated (distance coding) with a pulse of ~500 us
followed by a short gap of ~1000 us for a 0 bit or a long ~2000 us gap for a
1 bit, the sync gap is ~4000 us.

The data is grouped in 9 nibbles:

    [id0] [id1] [flags] [temp0] [temp1] [temp2] [const] [humi0] [humi1]

- The 8-bit id changes when the battery is changed in the sensor.
- flags are 4 bits B T C C
  - B is the battery status: 1=OK, 0=LOW
  - T is Test mode, 0=Normal, 1=Test
  - CC is the channel: 0=CH1, 1=CH2, 2=CH3
- temp is 12 bit signed scaled by 10
- const is always 1111 (0x0F)
- humidity is 8 bits

Test mode is entered if the "RES"-button ist held pressed while inserting batteries.
The sensor will send continuously every 2-15 secs. until the battery is reset.

The sensors can be bought at Clas Ohlsen (Nexus) and Pearl (infactory/FreeTec).
*/
static int nexus_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 36);
    if (r < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b = bitbuffer->bb[r];

    // we expect 36 bits but there might be a trailing 0 bit
    if (bitbuffer->bits_per_row[r] > 37) {
        return DECODE_ABORT_LENGTH;
    }

    if ((b[3] & 0xf0) != 0xf0) {
        return DECODE_ABORT_EARLY; // const not 1111
    }

    // Reduce false positives.
    if ((b[0] == 0 && b[2] == 0 && b[3] == 0)
            || (b[0] == 0xff &&  b[2] == 0xff && b[3] == 0xFF)) {
        return DECODE_ABORT_EARLY;
    }

    if ((b[1] & 0x30) == 0x30) {
        return DECODE_ABORT_EARLY; // channel not 1-3
    }
    int id       = b[0];
    int battery  = b[1] & 0x80;
    int testmode = b[1] & 0x40;
    int channel  = ((b[1] & 0x30) >> 4) + 1;
    int temp_raw = (int16_t)((b[1] << 12) | (b[2] << 4)); // sign-extend
    float temp_c = (temp_raw >> 4) * 0.1f;
    int humidity = (((b[3] & 0x0F) << 4) | (b[4] >> 4));

    data_t *data;
    if (humidity == 0x00) { // Thermo
        /* clang-format off */
        data = data_make(
                "model",         "",            DATA_STRING, "Nexus-T",
                "id",            "House Code",  DATA_INT,    id,
                "channel",       "Channel",     DATA_INT,    channel,
                "battery_ok",    "Battery",     DATA_INT,    !!battery,
                "temperature_C", "Temperature", DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
                "test",          "Test?",       DATA_COND,   testmode, DATA_INT,    !!testmode,
                NULL);
        /* clang-format on */
    }
    else { // Thermo/Hygro
        /* clang-format off */
        data = data_make(
                "model",         "",            DATA_STRING, "Nexus-TH",
                "id",            "House Code",  DATA_INT,    id,
                "channel",       "Channel",     DATA_INT,    channel,
                "battery_ok",    "Battery",     DATA_INT,    !!battery,
                "temperature_C", "Temperature", DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
                "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                "test",          "Test?",       DATA_COND,   testmode, DATA_INT,    !!testmode,
                NULL);
        /* clang-format on */
    }

    decoder_output_data(decoder, data);
    return 1;
}

/**
Nexus Sauna sensor with ID, temperature, battery and test flag.

The "Sauna sensor", sends 36 bits 6 times, the nibbles are:

    [id0] [id1] [flags] [const] [temp0] [temp1] [temp2] [temp2] [const2]

- The 8-bit id changes when the battery is changed in the sensor.
- flags are 4 bits B T C C, where:
  - B is the battery status: 1=OK, 0=LOW
  - T is Test mode, 0=Normal, 1=Test.
  To enter test mode, press and hold Tx/Send button while putting the last battery in, it will send values at ~2sec interval.
  - CC is the channel: It is always 11 (0x3) for CH4
- temp is 16 bit signed scaled by 10
- const is always 1111 (0x0F)
- const2 is always 0001 (0x1)
  To be exact, the "Sauna sensor" seems to send niblets 6 times with const2=0x1, and then seventh time sends just 35 bits, so last nibble is 0b000.
  Maybe this is "data-end" mark. I don't know. Anyway, it can be ignored here, cause data has been parsed already.

Sauna sensor kit is sold by IKH (CRX) and Motonet (Prego).
*/
static int nexus_sauna_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 36);
    if (r < 0) {
        return DECODE_ABORT_EARLY;
    }

    uint8_t *b = bitbuffer->bb[r];

    // we expect 36 bits but there might be a trailing 0 bit
    if (bitbuffer->bits_per_row[r] > 37) {
        return DECODE_ABORT_LENGTH;
    }
    if ((b[1] & 0xf) != 0xf) {
        return DECODE_ABORT_EARLY; // const not 1111
    }
    // Reduce false positives.
    if ((b[0] == 0) || ((b[4] & 0x10) != 0x10)) {
        return DECODE_ABORT_EARLY;
    }
    if ((b[1] & 0x30) != 0x30) {
        return DECODE_ABORT_EARLY; // channel not 4
    }

    int id       = b[0];
    int battery  = b[1] & 0x80;
    int testmode = b[1] & 0x40;
    int channel  = ((b[1] & 0x30) >> 4) + 1; // always 0x3 = CH4
    int temp_raw = (int16_t)((b[2] << 8) | (b[3])); // sign-extend
    float temp_c = temp_raw * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "Nexus-Sauna",
            "id",            "House Code",  DATA_INT,    id,
            "channel",       "Channel",     DATA_INT,    channel,
            "battery_ok",    "Battery",     DATA_INT,    !!battery,
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "test",          "Test?",       DATA_COND,   testmode,   DATA_INT,    !!testmode,
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
        "temperature_C",
        "humidity",
        "test",
        NULL,
};

r_device const nexus = {
        .name        = "Nexus, FreeTec NC-7345, NX-3980, Solight TE82S, TFA 30.3209 temperature/humidity sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 3000,
        .reset_limit = 5000,
        .decode_fn   = &nexus_decode,
        .priority    = 10, // Eliminate false positives by letting Rubicson-Temperature go earlier
        .fields      = output_fields,
};

static char const *const sauna_output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "test",
        NULL,
};

r_device const nexus_sauna = {
        .name        = "Nexus, CRX, Prego sauna temperature sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 3000,
        .reset_limit = 5000,
        .decode_fn   = &nexus_sauna_decode,
        .priority    = 10, // Eliminate false positives by letting Rubicson-Temperature go earlier
        .fields      = sauna_output_fields,
};
