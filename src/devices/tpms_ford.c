// Generated from tpms_ford.py
/** @file
    Ford TPMS decoder.
*/

#include "decoder.h"

static inline bool tpms_ford_validate_checksum(int sensor_id, int pressure_raw, int temp_byte, int flags, int checksum) {
  return (((((((((sensor_id >> 0x18) + ((sensor_id >> 0x10) & 0xff)) + ((sensor_id >> 8) & 0xff)) + (sensor_id & 0xff)) + pressure_raw) + temp_byte) + flags) & 0xff) == checksum);
}

static inline float tpms_ford_pressure_psi(int flags, int pressure_raw) {
  return ((((flags & 0x20) << 3) | pressure_raw) * 0.25);
}

static inline int tpms_ford_moving(int flags) {
  return ((flags & 0x44) == 0x44);
}

static inline int tpms_ford_learn(int flags) {
  return ((flags & 0x4c) == 8);
}

static inline int tpms_ford_code(int pressure_raw, int temp_byte, int flags) {
  return (((pressure_raw << 0x10) | (temp_byte << 8)) | flags);
}

static inline int tpms_ford_unknown(int flags) {
  return (flags & 0x90);
}

static inline int tpms_ford_unknown_3(int flags) {
  return (flags & 3);
}

static int tpms_ford_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  bitbuffer_invert(bitbuffer);
  uint8_t const preamble[] = { 0xaa, 0xa9 };
  if (bitbuffer->num_rows < 1)
      return DECODE_ABORT_LENGTH;
  unsigned tip_row = 0;
  unsigned offset = 0;
  int preamble_found = 0;
  for (unsigned row = 0; row < (unsigned)bitbuffer->num_rows && !preamble_found; ++row) {
    unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble, 16);
    if (pos < bitbuffer->bits_per_row[row]) {
      tip_row = row;
      offset = pos;
      preamble_found = 1;
    }
  }
  if (!preamble_found)
      return DECODE_ABORT_EARLY;
  offset += 16;
  bitbuffer_t packet_bits = {0};
  bitbuffer_manchester_decode(bitbuffer, tip_row, offset, &packet_bits, 160);
  if (packet_bits.bits_per_row[0] < 8)
      return DECODE_ABORT_LENGTH;
  bitbuffer = &packet_bits;
  uint8_t *b = bitbuffer->bb[0];
  unsigned bit_pos = 0;
  int sensor_id = bitrow_get_bits(b, bit_pos, 32);
  bit_pos += 32;
  int pressure_raw = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  int temp_byte = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  int flags = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  int checksum = bitrow_get_bits(b, bit_pos, 8);
  bit_pos += 8;
  if (!tpms_ford_validate_checksum(sensor_id, pressure_raw, temp_byte, flags, checksum))
      return DECODE_FAIL_MIC;
  char buf_id_0[64];
  snprintf(buf_id_0, sizeof(buf_id_0), "%08x", sensor_id);
  char buf_code_1[64];
  snprintf(buf_code_1, sizeof(buf_code_1), "%06x", tpms_ford_code(pressure_raw, temp_byte, flags));
  char buf_unknown_2[64];
  snprintf(buf_unknown_2, sizeof(buf_unknown_2), "%02x", tpms_ford_unknown(flags));
  char buf_unknown_3_3[64];
  snprintf(buf_unknown_3_3, sizeof(buf_unknown_3_3), "%01x", tpms_ford_unknown_3(flags));
  data_t *data = data_make(
          "model", "", DATA_STRING, "Ford",
          "type", "", DATA_STRING, "TPMS",
          "id", "", DATA_STRING, buf_id_0,
          "pressure_PSI", "Pressure", DATA_DOUBLE, (double)tpms_ford_pressure_psi(flags, pressure_raw),
          "moving", "Moving", DATA_INT, tpms_ford_moving(flags),
          "learn", "Learn", DATA_INT, tpms_ford_learn(flags),
          "code", "", DATA_STRING, buf_code_1,
          "unknown", "", DATA_STRING, buf_unknown_2,
          "unknown_3", "", DATA_STRING, buf_unknown_3_3,
          "mic", "Integrity", DATA_STRING, "CHECKSUM",
          NULL);
  decoder_output_data(decoder, data);
  return 1;
}

static char const *const output_fields[] = {
    "model",
    "type",
    "id",
    "pressure_PSI",
    "moving",
    "learn",
    "code",
    "unknown",
    "unknown_3",
    "mic",
    NULL,
};

r_device const tpms_ford = {
    .name        = "Ford TPMS",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 52.0,
    .long_width  = 52.0,
    .reset_limit = 150.0,
    .decode_fn   = &tpms_ford_decode,
    .fields      = output_fields,
};
