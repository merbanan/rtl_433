/** @file
    Prologue sensor protocol.
*/
/** @fn int prologue_callback(r_device *decoder, bitbuffer_t *bitbuffer)
Prologue sensor protocol,
also FreeTec NC-7104 sensor for FreeTec Weatherstation NC-7102,
and Pearl NC-7159-675.

The sensor sends 36 bits 7 times, before the first packet there is a sync pulse.
The packets are ppm modulated (distance coding) with a pulse of ~500 us
followed by a short gap of ~2000 us for a 0 bit or a long ~4000 us gap for a
1 bit, the sync gap is ~9000 us.

The data is grouped in 9 nibbles

    [type] [id0] [id1] [flags] [temp0] [temp1] [temp2] [humi0] [humi1]

- type: 4 bit fixed 1001 (9) or 0110 (5)
- id: 8 bit a random id that is generated when the sensor starts, could include battery status
  the same batteries often generate the same id
- flags(3): is 0 the battery status, 1 ok, 0 low, first reading always say low
- flags(2): is 1 when the sensor sends a reading when pressing the button on the sensor
- flags(1,0): the channel number that can be set by the sensor (1, 2, 3, X)
- temp: 12 bit signed scaled by 10
- humi: 8 bit always 11001100 (0xCC) if no humidity sensor is available

The sensor can be bought at Clas Ohlson.
*/

#include "decoder.h"

extern int alecto_checksum(r_device *decoder, bitrow_t *bb);

static int prologue_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b;
    data_t *data;
    int ret;

    uint8_t type;
    uint8_t id;
    uint8_t battery;
    uint8_t button;
    uint8_t channel;
    int16_t temp_raw;
    uint8_t humidity;

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

    /* Check for Alecto collision */
    ret = alecto_checksum(decoder, bitbuffer->bb);
    // if the checksum is correct, it's not prologue
    if (ret > 0)
        return DECODE_FAIL_SANITY;

    /* Prologue sensor */
    type     = b[0] >> 4;
    id       = ((b[0] & 0x0F) << 4) | ((b[1] & 0xF0) >> 4);
    battery  = b[1] & 0x08;
    button   = (b[1] & 0x04) >> 2;
    channel  = (b[1] & 0x03) + 1;
    temp_raw = (int16_t)((b[2] << 8) | (b[3] & 0xF0));
    temp_raw = temp_raw >> 4;
    humidity = ((b[3] & 0x0F) << 4) | (b[4] >> 4);

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, _X("Prologue-TH","Prologue sensor"),
            _X("subtype","id"),       "",            DATA_INT, type,
            _X("id","rid"),            "",            DATA_INT, id,
            "channel",       "Channel",     DATA_INT, channel,
            "battery",       "Battery",     DATA_STRING, battery ? "OK" : "LOW",
            "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_raw * 0.1,
            "button",        "Button",      DATA_INT, button,
            NULL);

    if (humidity != 0xcc) // 0xcc is "invalid"
        data = data_append(data,
                "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "subtype",
        "id",
        "rid", // TODO: delete this
        "channel",
        "battery",
        "temperature_C",
        "humidity",
        "button",
        NULL};

r_device prologue = {
        .name        = "Prologue, FreeTec NC-7104, NC-7159-675 temperature sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 7000,
        .reset_limit = 10000,
        .decode_fn   = &prologue_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
