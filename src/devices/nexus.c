/** @file
    Nexus temperature and optional humidity sensor protocol.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/** @fn int nexus_callback(r_device *decoder, bitbuffer_t *bitbuffer)
Nexus sensor protocol with ID, temperature and optional humidity

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
- flags are 4 bits B 0 C C, where B is the battery status: 1=OK, 0=LOW
- and CC is the channel: 0=CH1, 1=CH2, 2=CH3
- temp is 12 bit signed scaled by 10
- const is always 1111 (0x0F)
- humidity is 8 bits

The sensors can be bought at Clas Ohlsen (Nexus) and Pearl (infactory/FreeTec).
*/

#include "decoder.h"

// NOTE: this should really not be here
int rubicson_crc_check(uint8_t *b);

static int nexus_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int id, battery, channel, temp_raw, humidity;
    float temp_c;

    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 36);
    if (r < 0)
        return DECODE_ABORT_EARLY;

    b = bitbuffer->bb[r];

    // we expect 36 bits but there might be a trailing 0 bit
    if (bitbuffer->bits_per_row[r] > 37)
        return DECODE_ABORT_LENGTH;

    if ((b[3] & 0xf0) != 0xf0)
        return DECODE_ABORT_EARLY; // const not 1111

    // The nexus protocol will trigger on rubicson data, so calculate the rubicson crc and make sure
    // it doesn't match. By guesstimate it should generate a correct crc 1/255% of the times.
    // So less then 0.5% which should be acceptable.
    if ((b[0] == 0 && b[2] == 0 && b[3] == 0)
            || (b[0] == 0xff &&  b[2] == 0xff && b[3] == 0xFF)
            || rubicson_crc_check(b))
        return DECODE_ABORT_EARLY;

    id       = b[0];
    battery  = b[1] & 0x80;
    channel  = ((b[1] & 0x30) >> 4) + 1;
    temp_raw = (int16_t)((b[1] << 12) | (b[2] << 4)); // sign-extend
    temp_c   = (temp_raw >> 4) * 0.1f;
    humidity = (((b[3] & 0x0F) << 4) | (b[4] >> 4));

    if (humidity == 0x00) { // Thermo
        /* clang-format off */
        data = data_make(
                "model",         "",            DATA_STRING, "Nexus-T",
                "id",            "House Code",  DATA_INT,    id,
                "channel",       "Channel",     DATA_INT,    channel,
                "battery_ok",    "Battery",     DATA_INT,    !!battery,
                "temperature_C", "Temperature", DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
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
                NULL);
        /* clang-format on */
    }

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
        NULL,
};

r_device const nexus = {
        .name        = "Nexus, FreeTec NC-7345, NX-3980, Solight TE82S, TFA 30.3209 temperature/humidity sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1000,
        .long_width  = 2000,
        .gap_limit   = 3000,
        .reset_limit = 5000,
        .decode_fn   = &nexus_callback,
        .fields      = output_fields,
};
