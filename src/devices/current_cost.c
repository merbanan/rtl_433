// Generated from current_cost.py
/** @file
    CurrentCost Current Sensor decoder.
*/

#include "decoder.h"

static inline int current_cost_envir_meter_power0_W(int ch0_valid, int ch0_power) {
  return (ch0_valid * ch0_power);
}

static inline int current_cost_envir_meter_power1_W(int ch1_valid, int ch1_power) {
  return (ch1_valid * ch1_power);
}

static inline int current_cost_envir_meter_power2_W(int ch2_valid, int ch2_power) {
  return (ch2_valid * ch2_power);
}

static inline int current_cost_classic_meter_power0_W(int ch0_valid, int ch0_power) {
  return (ch0_valid * ch0_power);
}

static inline int current_cost_classic_meter_power1_W(int ch1_valid, int ch1_power) {
  return (ch1_valid * ch1_power);
}

static inline int current_cost_classic_meter_power2_W(int ch2_valid, int ch2_power) {
  return (ch2_valid * ch2_power);
}

static int current_cost_envir_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  bitbuffer_invert(bitbuffer);
  uint8_t const preamble[] = { 0x55, 0x55, 0x55, 0x55, 0xa4, 0x57 };
  if (bitbuffer->num_rows < 1)
      return DECODE_ABORT_LENGTH;
  unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 48);
  if (offset >= bitbuffer->bits_per_row[0])
      return DECODE_ABORT_EARLY;
  offset += 47;
  bitbuffer_t packet_bits = {0};
  bitbuffer_manchester_decode(bitbuffer, 0, offset, &packet_bits, 0);
  if (packet_bits.bits_per_row[0] < 8)
      return DECODE_ABORT_LENGTH;
  bitbuffer = &packet_bits;
  uint8_t *b = bitbuffer->bb[0];
  unsigned bit_pos = 0;
  int msg_type = bitrow_get_bits(b, bit_pos, 4);
  bit_pos += 4;
  int device_id = bitrow_get_bits(b, bit_pos, 12);
  bit_pos += 12;
  if ((msg_type == 0)) {
    int ch0_valid = bitrow_get_bits(b, bit_pos, 1);
    bit_pos += 1;
    int ch0_power = bitrow_get_bits(b, bit_pos, 15);
    bit_pos += 15;
    int ch1_valid = bitrow_get_bits(b, bit_pos, 1);
    bit_pos += 1;
    int ch1_power = bitrow_get_bits(b, bit_pos, 15);
    bit_pos += 15;
    int ch2_valid = bitrow_get_bits(b, bit_pos, 1);
    bit_pos += 1;
    int ch2_power = bitrow_get_bits(b, bit_pos, 15);
    bit_pos += 15;
    data_t *data = data_make(
            "model", "", DATA_STRING, "CurrentCost-EnviR",
            "id", "Device Id", DATA_INT, device_id,
            "power0_W", "Power 0", DATA_INT, current_cost_envir_meter_power0_W(ch0_valid, ch0_power),
            "power1_W", "Power 1", DATA_INT, current_cost_envir_meter_power1_W(ch1_valid, ch1_power),
            "power2_W", "Power 2", DATA_INT, current_cost_envir_meter_power2_W(ch2_valid, ch2_power),
            NULL);
    decoder_output_data(decoder, data);
    return 1;
  } else if ((msg_type == 4)) {
    bit_pos += 8;
    int sensor_type = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;
    int impulse = bitrow_get_bits(b, bit_pos, 32);
    bit_pos += 32;
    data_t *data = data_make(
            "model", "", DATA_STRING, "CurrentCost-EnviRCounter",
            "subtype", "Sensor Id", DATA_INT, sensor_type,
            "id", "Device Id", DATA_INT, device_id,
            "power0", "Counter", DATA_INT, impulse,
            NULL);
    decoder_output_data(decoder, data);
    return 1;
  }
  return DECODE_FAIL_SANITY;
}

static int current_cost_classic_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  bitbuffer_invert(bitbuffer);
  uint8_t const preamble[] = { 0xcc, 0xcc, 0xcc, 0xce, 0x91, 0x5d };
  if (bitbuffer->num_rows < 1)
      return DECODE_ABORT_LENGTH;
  unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 45);
  if (offset >= bitbuffer->bits_per_row[0])
      return DECODE_ABORT_EARLY;
  offset += 45;
  bitbuffer_t packet_bits = {0};
  bitbuffer_manchester_decode(bitbuffer, 0, offset, &packet_bits, 0);
  if (packet_bits.bits_per_row[0] < 8)
      return DECODE_ABORT_LENGTH;
  bitbuffer = &packet_bits;
  uint8_t *b = bitbuffer->bb[0];
  unsigned bit_pos = 0;
  int msg_type = bitrow_get_bits(b, bit_pos, 4);
  bit_pos += 4;
  int device_id = bitrow_get_bits(b, bit_pos, 12);
  bit_pos += 12;
  if ((msg_type == 0)) {
    int ch0_valid = bitrow_get_bits(b, bit_pos, 1);
    bit_pos += 1;
    int ch0_power = bitrow_get_bits(b, bit_pos, 15);
    bit_pos += 15;
    int ch1_valid = bitrow_get_bits(b, bit_pos, 1);
    bit_pos += 1;
    int ch1_power = bitrow_get_bits(b, bit_pos, 15);
    bit_pos += 15;
    int ch2_valid = bitrow_get_bits(b, bit_pos, 1);
    bit_pos += 1;
    int ch2_power = bitrow_get_bits(b, bit_pos, 15);
    bit_pos += 15;
    data_t *data = data_make(
            "model", "", DATA_STRING, "CurrentCost-TX",
            "id", "Device Id", DATA_INT, device_id,
            "power0_W", "Power 0", DATA_INT, current_cost_classic_meter_power0_W(ch0_valid, ch0_power),
            "power1_W", "Power 1", DATA_INT, current_cost_classic_meter_power1_W(ch1_valid, ch1_power),
            "power2_W", "Power 2", DATA_INT, current_cost_classic_meter_power2_W(ch2_valid, ch2_power),
            NULL);
    decoder_output_data(decoder, data);
    return 1;
  } else if ((msg_type == 4)) {
    bit_pos += 8;
    int sensor_type = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;
    int impulse = bitrow_get_bits(b, bit_pos, 32);
    bit_pos += 32;
    data_t *data = data_make(
            "model", "", DATA_STRING, "CurrentCost-Counter",
            "subtype", "Sensor Id", DATA_INT, sensor_type,
            "id", "Device Id", DATA_INT, device_id,
            "power0", "Counter", DATA_INT, impulse,
            NULL);
    decoder_output_data(decoder, data);
    return 1;
  }
  return DECODE_FAIL_SANITY;
}

static int current_cost_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  bitbuffer_t saved = *bitbuffer;
  int ret = DECODE_ABORT_EARLY;
  ret = current_cost_envir_decode(decoder, bitbuffer);
  if (ret > 0) return ret;
  *bitbuffer = saved;
  ret = current_cost_classic_decode(decoder, bitbuffer);
  if (ret > 0) return ret;
  return ret;
}

static char const *const output_fields[] = {
    "model",
    "msg_type",
    "device_id",
    "id",
    "power0_W",
    "power1_W",
    "power2_W",
    "subtype",
    "power0",
    NULL,
};

r_device const current_cost = {
    .name        = "CurrentCost Current Sensor",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 250.0,
    .long_width  = 250.0,
    .reset_limit = 8000.0,
    .decode_fn   = &current_cost_decode,
    .fields      = output_fields,
};
