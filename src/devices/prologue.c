/** @file
    Prologue sensor protocol.
*/
/** @fn int prologue_callback(r_device *decoder, bitbuffer_t *bitbuffer)
Prologue sensor protocol,
also FreeTec NC-7104 sensor for FreeTec Weatherstation NC-7102,
and Pearl NC-7159-675.
The sensor can be bought at Clas Ohlson.

Note: this is a false positive for AlectoV1.

The sensor sends 36 bits 7 times, before the first packet there is a sync pulse.
The packets are ppm modulated (distance coding) with a pulse of ~500 us
followed by a short gap of ~2000 us for a 0 bit or a long ~4000 us gap for a
1 bit, the sync gap is ~9000 us.

The data is grouped in 9 nibbles

    [type] [id0] [id1] [flags] [temp0] [temp1] [temp2] [humi0] [humi1]

- type: 4 bit fixed 1001 (9) or 0110 (5)
- id: 8 bit a random id that is generated when the sensor starts, could include battery status
  the same batteries often generate the same id
- flags(3): is 0 when the battery is low, otherwise 1 (ok), first reading always says low
- flags(2): is 1 when the sensor sends a reading when pressing the button on the sensor
- flags(1,0): the channel number that can be set by the sensor (1, 2, 3, X)
- temp: 12 bit signed scaled by 10
- humi: 8 bit always 11001100 (0xCC) if no humidity sensor is available

*/

#include "decoder.h"

static int prologue_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b;
    data_t *data;

    int type;
    int id;
    int battery;
    int button;
    int channel;
    int temp_raw;
    int humidity;

    if (bitbuffer->bits_per_row[0] <= 8 && bitbuffer->bits_per_row[0] != 0)
        return DECODE_ABORT_EARLY; // Alecto/Auriol-v2 has 8 sync bits, reduce false positive

    int r = bitbuffer_find_repeated_row(bitbuffer, 4, 36); // only 3 repeats will give false positives for Alecto/Auriol-v2
    if (r < 0)
        return DECODE_ABORT_EARLY;

    if (bitbuffer->bits_per_row[r] > 37) // we expect 36 bits but there might be a trailing 0 bit
        return DECODE_ABORT_LENGTH;

    b = bitbuffer->bb[r];

    if ((b[0] & 0xF0) != 0x90 && (b[0] & 0xF0) != 0x50)
        return DECODE_FAIL_SANITY;

    // Prologue/ThermoPro-TX2 sensor
    type     = b[0] >> 4;
    id       = ((b[0] & 0x0F) << 4) | ((b[1] & 0xF0) >> 4);
    battery  = b[1] & 0x08;
    button   = (b[1] & 0x04) >> 2;
    channel  = (b[1] & 0x03) + 1;
    temp_raw = (int16_t)((b[2] << 8) | (b[3] & 0xF0)); // uses sign-extend
    temp_raw = temp_raw >> 4;
    humidity = ((b[3] & 0x0F) << 4) | (b[4] >> 4);

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, "Prologue-TH",
            "subtype",       "",            DATA_INT,    type,
            "id",            "",            DATA_INT,    id,
            "channel",       "Channel",     DATA_INT,    channel,
            "battery_ok",    "Battery",     DATA_INT,    !!battery,
            "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_raw * 0.1,
            "humidity",      "Humidity",    DATA_COND,   humidity != 0xcc, DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "button",        "Button",      DATA_INT,    button,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "subtype",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "button",
        NULL,
};

r_device const prologue = {
        .name        = "Prologue, FreeTec NC-7104, NC-7159-675 temperature sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 7000,
        .reset_limit = 10000,
        .decode_fn   = &prologue_callback,
        .priority    = 10, // Alecto collision, if Alecto checksum is correct it's not Prologue/ThermoPro-TX2
        .fields      = output_fields,
};
