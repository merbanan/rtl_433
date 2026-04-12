// Generated from akhan_100F14.py
/** @file
    Akhan 100F14 remote keyless entry decoder.
*/

#include "decoder.h"
#include "akhan_100F14.h"

static constexpr int akhan_100F14_notb0(int b0) {
  return ((~b0) & 0xff);
}

static constexpr int akhan_100F14_notb1(int b1) {
  return ((~b1) & 0xff);
}

static constexpr int akhan_100F14_notb2(int b2) {
  return ((~b2) & 0xff);
}

static constexpr int akhan_100F14_id(int notb0, int notb1, int notb2) {
  return (((notb0 << 0xc) | (notb1 << 4)) | (notb2 >> 4));
}

static constexpr int akhan_100F14_cmd(int notb2) {
  return (notb2 & 0xf);
}

static constexpr bool akhan_100F14_validate_cmd(int cmd) {
  return ((((cmd == 1) | (cmd == 2)) | (cmd == 4)) | (cmd == 8));
}

static int akhan_100F14_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  int row = bitbuffer_find_repeated_row(bitbuffer, 1, 25);
  if (row < 0)
      return DECODE_ABORT_EARLY;
  if (bitbuffer->bits_per_row[row] != 25)
      return DECODE_ABORT_LENGTH;
  uint8_t *b = bitbuffer->bb[row];
  unsigned bit_pos = 0;
  int b0 = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  int b1 = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  int b2 = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  bit_pos += 1;
  if (!akhan_100F14_validate_cmd(cmd))
      return DECODE_FAIL_SANITY;
  data_t *data = data_make(
          "model", "", DATA_STRING, "Akhan-100F14",
          "id", "ID (20bit)", DATA_FORMAT, "0x%x", DATA_INT, akhan_100F14_id(notb0, notb1, notb2),
          "data", "Data (4bit)", DATA_STRING, akhan_100F14_data_str(cmd),
          NULL);
  decoder_output_data(decoder, data);
  return 1;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "data",
    NULL,
};

r_device const akhan_100F14 = {
    .name        = "Akhan 100F14 remote keyless entry",
    .modulation  = OOK_PULSE_PWM,
    .short_width = 316.0,
    .long_width  = 1020.0,
    .reset_limit = 1800.0,
    .sync_width  = 0.0,
    .tolerance   = 80.0,
    .disabled    = 1,
    .decode_fn   = &akhan_100F14_decode,
    .fields      = output_fields,
};
