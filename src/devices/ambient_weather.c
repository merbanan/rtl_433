// Generated from ambient_weather.py
/** @file
    Ambient Weather F007TH thermo-hygrometer (and related senders).

    Generated decoder; LFSR MIC and sanity checks are inlined via ``validate_*`` hooks
    and ``LfsrDigest8``.

    Differs from the old hand-written decoder: only preamble ``0x145`` (12 bits) is
    searched, first match per row from bit 0 only (no second inverted preamble, no
    sliding retry after MIC failure).

    Reference: former ``src/devices/ambient_weather.c``.
*/

#include "decoder.h"

/** @fn static int ambient_weather_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    Ambient Weather F007TH thermo-hygrometer (and related senders).

    Generated decoder; LFSR MIC and sanity checks are inlined via ``validate_*`` hooks
    and ``LfsrDigest8``.

    Differs from the old hand-written decoder: only preamble ``0x145`` (12 bits) is
    searched, first match per row from bit 0 only (no second inverted preamble, no
    sliding retry after MIC failure).

    Reference: former ``src/devices/ambient_weather.c``.
*/
static int ambient_weather_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    unsigned search_row = 0;
    uint8_t const preamble[] = {0x01, 0x45};
    if (bitbuffer->num_rows < 1)
        return DECODE_ABORT_LENGTH;
    unsigned offset = 0;
    int preamble_found = 0;
    for (unsigned row = 0; row < (unsigned)bitbuffer->num_rows && !preamble_found; ++row) {
        unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble, 12);
        if (pos < bitbuffer->bits_per_row[row]) {
            search_row = row;
            offset = pos;
            preamble_found = 1;
        }
    }
    if (!preamble_found)
        return DECODE_ABORT_EARLY;
    offset += 8;

    uint8_t b[6];
    bitbuffer_extract_bytes(bitbuffer, search_row, offset, b, 48);

    int b0 = bitrow_get_bits(b, 0, 8);
    int b1 = bitrow_get_bits(b, 8, 8);
    int sensor_id = b1;
    int b2 = bitrow_get_bits(b, 16, 8);
    int battery_ok = ((b2 & 0x80) == 0);
    int channel = (((b2 & 0x70) >> 4) + 1);
    int b3 = bitrow_get_bits(b, 24, 8);
    float temperature_F = (((((b2 & 0xf) << 8) | b3) - 0x190) * 0.1);
    if (!(((((b2 & 0xf) << 8) | b3) < 0xf00)))
        return DECODE_FAIL_SANITY;

    int b4 = bitrow_get_bits(b, 32, 8);
    int humidity = b4;
    if (!((b4 <= 0x64)))
        return DECODE_FAIL_SANITY;

    int b5 = bitrow_get_bits(b, 40, 8);
    if (!((((lfsr_digest8((uint8_t const[]){b0, b1, b2, b3, b4}, 5, 0x98, 0x3e)) ^ 0x64) == b5)))
        return DECODE_FAIL_MIC;



    /* clang-format off */
    data_t *data = data_make(
        "model", "", DATA_STRING, "Ambientweather-F007TH",
        "id", "House Code", DATA_INT, sensor_id,
        "channel", "Channel", DATA_INT, channel,
        "battery_ok", "Battery", DATA_INT, battery_ok,
        "temperature_F", "Temperature", DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature_F,
        "humidity", "Humidity", DATA_FORMAT, "%u %%", DATA_INT, humidity,
        "mic", "Integrity", DATA_STRING, "CRC",
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
    "temperature_F",
    "humidity",
    "mic",
    NULL,
};

r_device const ambient_weather = {
    .name        = "Ambient Weather F007TH, TFA 30.3208.02, SwitchDocLabs F016TH temperature sensor",
    .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width = 500.0,
    .long_width  = 0.0,
    .reset_limit = 2400.0,
    .decode_fn   = &ambient_weather_decode,
    .fields      = output_fields,
};
