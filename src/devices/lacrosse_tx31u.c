// Generated from lacrosse_tx31u.py
/** @file
    LaCrosse TX31U-IT, The Weather Channel WS-1910TWC-IT decoder.
*/

#include "decoder.h"
#include "lacrosse_tx31u.h"

static int lacrosse_tx31u_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  uint8_t const preamble[] = { 0xaa, 0xaa, 0x2d, 0xd4 };
  if (bitbuffer->num_rows < 1)
      return DECODE_ABORT_LENGTH;
  unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 32);
  if (offset >= bitbuffer->bits_per_row[0])
      return DECODE_ABORT_EARLY;
  offset += 32;
  uint8_t *b = bitbuffer->bb[0];
  unsigned bit_pos = offset;
  if (bitrow_get_bits(b, bit_pos, 4) != 0xa)
      return DECODE_FAIL_SANITY;
  bit_pos += 4;
  int sensor_id = bitrow_get_bits(b, bit_pos, 6);
  bit_pos += 6;
  int training = bitrow_get_bits(b, bit_pos, 1);
  bit_pos += 1;
  int no_ext = bitrow_get_bits(b, bit_pos, 1);
  bit_pos += 1;
  int battery_low = bitrow_get_bits(b, bit_pos, 1);
  bit_pos += 1;
  int measurements = bitrow_get_bits(b, bit_pos, 3);
  bit_pos += 3;
  int readings_sensor_type[measurements];
  int readings_reading[measurements];
  for (int _i = 0; _i < measurements; _i++) {
    readings_sensor_type[_i] = bitrow_get_bits(b, bit_pos, 4);
    bit_pos += 4;
    readings_reading[_i] = bitrow_get_bits(b, bit_pos, 12);
    bit_pos += 12;
  }
  int crc = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  if (!lacrosse_tx31u_validate_crc(measurements, crc))
      return DECODE_FAIL_MIC;
  data_t *data = data_make(
          "model", "", DATA_STRING, "LaCrosse TX31U-IT, The Weather Channel WS-1910TWC-IT",
          "sensor_id", "", DATA_INT, sensor_id,
          "training", "", DATA_INT, training,
          "no_ext", "", DATA_INT, no_ext,
          "battery_low", "", DATA_INT, battery_low,
          "measurements", "", DATA_INT, measurements,
          "crc", "", DATA_INT, crc,
          "temperature_c", "", DATA_DOUBLE, (double)lacrosse_tx31u_temperature_c(readings_sensor_type, readings_reading, measurements),
          "humidity", "", DATA_INT, lacrosse_tx31u_humidity(readings_sensor_type, readings_reading, measurements),
          NULL);
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
