// Generated from ambient_weather.py
/** @file
    Ambient Weather F007TH, TFA 30.3208.02, SwitchDocLabs F016TH temperature sensor decoder.
*/

#include <stdbool.h>

#include "decoder.h"

static inline bool ambient_weather_validate_mic(int b0, int b1, int b2, int b3, int b4, int b5) {
  return (((lfsr_digest8((uint8_t const[]){b0, b1, b2, b3, b4}, 5, 0x98, 0x3e)) ^ 0x64) == b5);
}

static inline bool ambient_weather_validate_sanity_humidity(int b4) {
  return (b4 <= 0x64);
}

static inline bool ambient_weather_validate_sanity_temperature(int b2, int b3) {
  return ((((b2 & 0xf) << 8) | b3) < 0xf00);
}

static inline int ambient_weather_sensor_id(int b1) {
  return b1;
}

static inline int ambient_weather_battery_ok(int b2) {
  return ((b2 & 0x80) == 0);
}

static inline int ambient_weather_channel(int b2) {
  return (((b2 & 0x70) >> 4) + 1);
}

static inline float ambient_weather_temperature_F(int b2, int b3) {
  return (((((b2 & 0xf) << 8) | b3) - 0x190) * 0.1);
}

static inline int ambient_weather_humidity(int b4) {
  return b4;
}

/**
Ambient Weather F007TH, TFA 30.3208.02, SwitchDocLabs F016TH temperature sensor decoder.
*/
static int ambient_weather_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
  uint8_t const preamble[] = { 0x01, 0x45 };
  if (bitbuffer->num_rows < 1)
      return DECODE_ABORT_LENGTH;
  unsigned tip_row = 0;
  unsigned offset = 0;
  int preamble_found = 0;
  for (unsigned row = 0; row < (unsigned)bitbuffer->num_rows && !preamble_found; ++row) {
    unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble, 12);
    if (pos < bitbuffer->bits_per_row[row]) {
      tip_row = row;
      offset = pos;
      preamble_found = 1;
    }
  }
  if (!preamble_found)
      return DECODE_ABORT_EARLY;
  offset += 8;
  uint8_t *b = bitbuffer->bb[tip_row];
  unsigned bit_pos = offset;
  int b0 = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  int b1 = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  int b2 = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  int b3 = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  if (!ambient_weather_validate_sanity_temperature(b2, b3))
      return DECODE_FAIL_SANITY;
  int b4 = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  if (!ambient_weather_validate_sanity_humidity(b4))
      return DECODE_FAIL_SANITY;
  int b5 = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  if (!ambient_weather_validate_mic(b0, b1, b2, b3, b4, b5))
      return DECODE_FAIL_MIC;
  data_t *data = data_make(
          "model", "", DATA_STRING, "Ambientweather-F007TH",
          "id", "House Code", DATA_INT, ambient_weather_sensor_id(b1),
          "channel", "Channel", DATA_INT, ambient_weather_channel(b2),
          "battery_ok", "Battery", DATA_INT, ambient_weather_battery_ok(b2),
          "temperature_F", "Temperature", DATA_FORMAT, "%.1f F", DATA_DOUBLE, (double)ambient_weather_temperature_F(b2, b3),
          "humidity", "Humidity", DATA_FORMAT, "%u %%", DATA_INT, ambient_weather_humidity(b4),
          "mic", "Integrity", DATA_STRING, "CRC",
          NULL);
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
