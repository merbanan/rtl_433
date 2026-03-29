// Generated from lacrosse_tx31u.py
#include "decoder.h"
#include "lacrosse_tx31u.h"

static int lacrosse_tx31u_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0xaa, 0x2d, 0xd4};
    if (bitbuffer->num_rows < 1)
        return DECODE_ABORT_LENGTH;
    unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 32);
    if (offset >= bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;
    offset += 32;

    uint8_t b[19];
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 152);

    if (bitrow_get_bits(b, 0, 4) != 0xa)
        return DECODE_FAIL_SANITY;
    int sensor_id = bitrow_get_bits(b, 4, 6);
    int training = bitrow_get_bits(b, 10, 1);
    int no_ext = bitrow_get_bits(b, 11, 1);
    int battery_low = bitrow_get_bits(b, 12, 1);
    int measurements = bitrow_get_bits(b, 13, 3);
    int readings_sensor_type[8];
    int readings_reading[8];
    int bit_pos = 16;
    for (int _i = 0; _i < measurements && _i < 8; _i++) {
        readings_sensor_type[_i] = bitrow_get_bits(b, bit_pos, 4);
        bit_pos += 4;
        readings_reading[_i] = bitrow_get_bits(b, bit_pos, 12);
        bit_pos += 12;
    }

    int crc = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;

    int valid = lacrosse_tx31u_validate(b, measurements, crc);
    if (valid != 0)
        return valid;

    float temperature_c = lacrosse_tx31u_temperature_c(readings_sensor_type, readings_reading, measurements);
    int humidity = lacrosse_tx31u_humidity(readings_sensor_type, readings_reading, measurements);

    /* clang-format off */
    data_t *data = data_make(
        "model", "", DATA_STRING, "LaCrosse TX31U-IT, The Weather Channel WS-1910TWC-IT",
        "sensor_id", "", DATA_INT, sensor_id,
        "training", "", DATA_INT, training,
        "no_ext", "", DATA_INT, no_ext,
        "battery_low", "", DATA_INT, battery_low,
        "measurements", "", DATA_INT, measurements,
        "crc", "", DATA_INT, crc,
        "temperature_c", "", DATA_DOUBLE, temperature_c,
        "humidity", "", DATA_INT, humidity,
        NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
    "model",
    "sensor_id",
    "training",
    "no_ext",
    "battery_low",
    "measurements",
    "crc",
    "temperature_c",
    "humidity",
    NULL,
};

r_device const lacrosse_tx31u = {
    .name        = "LaCrosse TX31U-IT, The Weather Channel WS-1910TWC-IT",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 116.0,
    .long_width  = 116.0,
    .reset_limit = 20000.0,
    .decode_fn   = &lacrosse_tx31u_decode,
    .fields      = output_fields,
};
