// Generated from acurite_01185m.py
/** @file
    Acurite Grill/Meat Thermometer 01185M decoder.
*/

#include <stdbool.h>

#include "decoder.h"
#include "acurite_01185m.h"

static inline float acurite_01185m_BothProbes_temp1_f(int data_temp1_raw) {
  return ((data_temp1_raw - 0x384) * 0.1);
}

static inline float acurite_01185m_BothProbes_temp2_f(int data_temp2_raw) {
  return ((data_temp2_raw - 0x384) * 0.1);
}

static inline int acurite_01185m_BothProbes_battery_ok(int data_battery_low) {
  return (data_battery_low == 0);
}

static inline float acurite_01185m_MeatOnly_temp1_f(int data_temp1_raw) {
  return ((data_temp1_raw - 0x384) * 0.1);
}

static inline float acurite_01185m_MeatOnly_temp2_f(int data_temp2_raw) {
  return ((data_temp2_raw - 0x384) * 0.1);
}

static inline int acurite_01185m_MeatOnly_battery_ok(int data_battery_low) {
  return (data_battery_low == 0);
}

static inline float acurite_01185m_AmbientOnly_temp1_f(int data_temp1_raw) {
  return ((data_temp1_raw - 0x384) * 0.1);
}

static inline float acurite_01185m_AmbientOnly_temp2_f(int data_temp2_raw) {
  return ((data_temp2_raw - 0x384) * 0.1);
}

static inline int acurite_01185m_AmbientOnly_battery_ok(int data_battery_low) {
  return (data_battery_low == 0);
}

static inline float acurite_01185m_NoProbes_temp1_f(int data_temp1_raw) {
  return ((data_temp1_raw - 0x384) * 0.1);
}

static inline float acurite_01185m_NoProbes_temp2_f(int data_temp2_raw) {
  return ((data_temp2_raw - 0x384) * 0.1);
}

static inline int acurite_01185m_NoProbes_battery_ok(int data_battery_low) {
  return (data_battery_low == 0);
}

/**
Acurite Grill/Meat Thermometer 01185M decoder.
*/
static int acurite_01185m_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
  bitbuffer_invert(bitbuffer);
  for (int i = 0; i < bitbuffer->num_rows; ++i)
      reflect_bytes(bitbuffer->bb[i], (bitbuffer->bits_per_row[i] + 7) / 8);
  uint8_t *b = bitbuffer->bb[0];
  unsigned bit_pos = 0;
  int data_id[BITBUF_ROWS];
  int data_battery_low[BITBUF_ROWS];
  int data_mid[BITBUF_ROWS];
  int data_channel[BITBUF_ROWS];
  int data_temp1_raw[BITBUF_ROWS];
  int data_temp2_raw[BITBUF_ROWS];
  int data_checksum[BITBUF_ROWS];
  int result = 0;
  for (int _r = 0; _r < bitbuffer->num_rows; ++_r) {
    if (bitbuffer->bits_per_row[_r] != 56) {
      result = DECODE_ABORT_LENGTH;
      continue;
    }
    uint8_t *b = bitbuffer->bb[_r];
    unsigned bit_pos = 0;
    data_id[_r] = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;
    data_battery_low[_r] = bitrow_get_bits(b, bit_pos, 1);
    bit_pos += 1;
    data_mid[_r] = bitrow_get_bits(b, bit_pos, 3);
    bit_pos += 3;
    data_channel[_r] = bitrow_get_bits(b, bit_pos, 4);
    bit_pos += 4;
    data_temp1_raw[_r] = bitrow_get_bits(b, bit_pos, 16);
    bit_pos += 16;
    data_temp2_raw[_r] = bitrow_get_bits(b, bit_pos, 16);
    bit_pos += 16;
    data_checksum[_r] = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;
    if (!acurite_01185m_validate_checksum(data_id[_r], data_battery_low[_r], data_mid[_r], data_channel[_r], data_temp1_raw[_r], data_temp2_raw[_r], data_checksum[_r])) {
      result = DECODE_FAIL_MIC;
      continue;
    }
    if (((((data_temp1_raw[_r] > 0xc8) & (data_temp1_raw[_r] < 0x1b58)) & (data_temp2_raw[_r] > 0xc8)) & (data_temp2_raw[_r] < 0x1b58))) {
      data_t *data = data_make(
              "model", "", DATA_STRING, "Acurite-01185M",
              "id", "", DATA_INT, data_id[_r],
              "channel", "", DATA_INT, data_channel[_r],
              "battery_ok", "Battery", DATA_INT, acurite_01185m_BothProbes_battery_ok(data_battery_low[_r]),
              "temperature_1_F", "Meat", DATA_FORMAT, "%.1f F", DATA_DOUBLE, (double)acurite_01185m_BothProbes_temp1_f(data_temp1_raw[_r]),
              "temperature_2_F", "Ambient", DATA_FORMAT, "%.1f F", DATA_DOUBLE, (double)acurite_01185m_BothProbes_temp2_f(data_temp2_raw[_r]),
              "mic", "Integrity", DATA_STRING, "CHECKSUM",
              NULL);
      decoder_output_data(decoder, data);
      return 1;
    } else if ((((data_temp1_raw[_r] > 0xc8) & (data_temp1_raw[_r] < 0x1b58)) & ((data_temp2_raw[_r] <= 0xc8) | (data_temp2_raw[_r] >= 0x1b58)))) {
      data_t *data = data_make(
              "model", "", DATA_STRING, "Acurite-01185M",
              "id", "", DATA_INT, data_id[_r],
              "channel", "", DATA_INT, data_channel[_r],
              "battery_ok", "Battery", DATA_INT, acurite_01185m_MeatOnly_battery_ok(data_battery_low[_r]),
              "temperature_1_F", "Meat", DATA_FORMAT, "%.1f F", DATA_DOUBLE, (double)acurite_01185m_MeatOnly_temp1_f(data_temp1_raw[_r]),
              "mic", "Integrity", DATA_STRING, "CHECKSUM",
              NULL);
      decoder_output_data(decoder, data);
      return 1;
    } else if (((((data_temp1_raw[_r] <= 0xc8) | (data_temp1_raw[_r] >= 0x1b58)) & (data_temp2_raw[_r] > 0xc8)) & (data_temp2_raw[_r] < 0x1b58))) {
      data_t *data = data_make(
              "model", "", DATA_STRING, "Acurite-01185M",
              "id", "", DATA_INT, data_id[_r],
              "channel", "", DATA_INT, data_channel[_r],
              "battery_ok", "Battery", DATA_INT, acurite_01185m_AmbientOnly_battery_ok(data_battery_low[_r]),
              "temperature_2_F", "Ambient", DATA_FORMAT, "%.1f F", DATA_DOUBLE, (double)acurite_01185m_AmbientOnly_temp2_f(data_temp2_raw[_r]),
              "mic", "Integrity", DATA_STRING, "CHECKSUM",
              NULL);
      decoder_output_data(decoder, data);
      return 1;
    } else if ((((data_temp1_raw[_r] <= 0xc8) | (data_temp1_raw[_r] >= 0x1b58)) & ((data_temp2_raw[_r] <= 0xc8) | (data_temp2_raw[_r] >= 0x1b58)))) {
      data_t *data = data_make(
              "model", "", DATA_STRING, "Acurite-01185M",
              "id", "", DATA_INT, data_id[_r],
              "channel", "", DATA_INT, data_channel[_r],
              "battery_ok", "Battery", DATA_INT, acurite_01185m_NoProbes_battery_ok(data_battery_low[_r]),
              "mic", "Integrity", DATA_STRING, "CHECKSUM",
              NULL);
      decoder_output_data(decoder, data);
      return 1;
    }
    return DECODE_FAIL_SANITY;
  }
  return result;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "channel",
    "battery_ok",
    "temperature_1_F",
    "temperature_2_F",
    "mic",
    NULL,
};

r_device const acurite_01185m = {
    .name        = "Acurite Grill/Meat Thermometer 01185M",
    .modulation  = OOK_PULSE_PWM,
    .short_width = 840.0,
    .long_width  = 2070.0,
    .reset_limit = 6000.0,
    .gap_limit   = 3000.0,
    .sync_width  = 6600.0,
    .decode_fn   = &acurite_01185m_decode,
    .fields      = output_fields,
};
