// Generated from abmt.py
/** @file
    Amazon Basics Meat Thermometer decoder.
*/

#include "decoder.h"

static constexpr float abmt_temperature_c(int temp_bcd_byte, int temp_last_byte) {
  return ((((0xa * (temp_bcd_byte >> 4)) + (temp_bcd_byte & 0xf)) * 0xa) + (temp_last_byte >> 4));
}

static int abmt_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  int row = bitbuffer_find_repeated_row(bitbuffer, 4, 90);
  if (row < 0)
      return DECODE_ABORT_EARLY;
  if (bitbuffer->bits_per_row[row] > 120)
      return DECODE_ABORT_LENGTH;
  uint8_t const preamble[] = { 0x55, 0xaa, 0xaa };
  if (bitbuffer->num_rows < 1)
      return DECODE_ABORT_LENGTH;
  unsigned offset = bitbuffer_search(bitbuffer, row, 0, preamble, 24);
  if (offset >= bitbuffer->bits_per_row[row])
      return DECODE_ABORT_EARLY;
  if (offset < 72)
      return DECODE_FAIL_SANITY;
  offset -= 72;
  bitbuffer_t packet_bits = {0};
  bitbuffer_manchester_decode(bitbuffer, row, offset, &packet_bits, 48);
  if (packet_bits.bits_per_row[0] < 8)
      return DECODE_ABORT_LENGTH;
  bitbuffer = &packet_bits;
  bitbuffer_invert(bitbuffer);
  uint8_t *b = bitbuffer->bb[0];
  unsigned bit_pos = 0;
  int id = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  bit_pos += 8;
  bit_pos += 8;
  int temp_bcd_byte = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  int temp_last_byte = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  bit_pos += 8;
  data_t *data = data_make(
          "model", "", DATA_STRING, "Basics-Meat",
          "id", "Id", DATA_INT, id,
          "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)abmt_temperature_c(temp_bcd_byte, temp_last_byte),
          NULL);
  decoder_output_data(decoder, data);
  return 1;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "temperature_C",
    NULL,
};

r_device const abmt = {
    .name        = "Amazon Basics Meat Thermometer",
    .modulation  = OOK_PULSE_PCM,
    .short_width = 550.0,
    .long_width  = 550.0,
    .reset_limit = 5000.0,
    .gap_limit   = 2000.0,
    .decode_fn   = &abmt_decode,
    .fields      = output_fields,
};
