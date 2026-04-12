// Generated from thermopro_tp211b.py
/** @file
    ThermoPro TP211B Thermometer decoder.
*/

#include "decoder.h"
#include "thermopro_tp211b.h"

static inline float thermopro_tp211b_temperature_c(int temp_raw) {
  return ((temp_raw - 0x1f4) * 0.1);
}

static int thermopro_tp211b_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  uint8_t const preamble[] = { 0x55, 0x2d, 0xd4 };
  if (bitbuffer->num_rows < 1)
      return DECODE_ABORT_LENGTH;
  unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 24);
  if (offset >= bitbuffer->bits_per_row[0])
      return DECODE_ABORT_EARLY;
  offset += 24;
  uint8_t *b = bitbuffer->bb[0];
  unsigned bit_pos = offset;
  int id = bitrow_get_bits(b, bit_pos, 24);
  bit_pos += 24;
  int flags = bitrow_get_bits(b, bit_pos, 4);
  bit_pos += 4;
  int temp_raw = bitrow_get_bits(b, bit_pos, 12);
  bit_pos += 12;
  if (bitrow_get_bits(b, bit_pos, 8) != 0xaa)
      return DECODE_FAIL_SANITY;
  bit_pos += 8;
  int checksum = bitrow_get_bits(b, bit_pos, 16);
  bit_pos += 16;
  if (!thermopro_tp211b_validate_checksum(id, flags, temp_raw, checksum))
      return DECODE_FAIL_MIC;
  data_t *data = data_make(
          "model", "", DATA_STRING, "ThermoPro-TP211B",
          "id", "", DATA_INT, id,
          "temperature_c", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)thermopro_tp211b_temperature_c(temp_raw),
          "mic", "Integrity", DATA_STRING, "CHECKSUM",
          NULL);
  decoder_output_data(decoder, data);
  return 1;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "temperature_c",
    "mic",
    NULL,
};

r_device const thermopro_tp211b = {
    .name        = "ThermoPro TP211B Thermometer",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 105.0,
    .long_width  = 105.0,
    .reset_limit = 1500.0,
    .decode_fn   = &thermopro_tp211b_decode,
    .fields      = output_fields,
};
