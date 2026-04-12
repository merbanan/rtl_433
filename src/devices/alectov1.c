// Generated from alectov1.py
/** @file
    AlectoV1 Weather Sensor (Alecto WS3500 WS4500 Ventus W155/W044 Oregon) decoder.
*/

#include "decoder.h"
#include "alectov1.h"

static inline int alectov1_Wind0_sensor_id(int *cells_b0) {
  return (reverse8((cells_b0[1])));
}

static inline int alectov1_Wind0_channel(int *cells_b0) {
  return (((cells_b0[1]) & 0xc) >> 2);
}

static inline int alectov1_Wind0_battery_ok(int *cells_b1) {
  return ((((cells_b1[1]) & 0x80) >> 7) == 0);
}

static inline float alectov1_Wind0_wind_avg_m_s(int *cells_b3) {
  return ((reverse8((cells_b3[1]))) * 0.2);
}

static inline float alectov1_Wind0_wind_max_m_s(int *cells_b3) {
  return ((reverse8((cells_b3[5]))) * 0.2);
}

static inline int alectov1_Wind0_wind_dir_deg(int *cells_b1, int *cells_b2) {
  return (((reverse8((cells_b2[5]))) << 1) | ((cells_b1[5]) & 1));
}

static inline int alectov1_Wind4_sensor_id(int *cells_b0) {
  return (reverse8((cells_b0[1])));
}

static inline int alectov1_Wind4_channel(int *cells_b0) {
  return (((cells_b0[1]) & 0xc) >> 2);
}

static inline int alectov1_Wind4_battery_ok(int *cells_b1) {
  return ((((cells_b1[1]) & 0x80) >> 7) == 0);
}

static inline float alectov1_Wind4_wind_avg_m_s(int *cells_b3) {
  return ((reverse8((cells_b3[5]))) * 0.2);
}

static inline int alectov1_Rain_sensor_id(int *cells_b0) {
  return (reverse8((cells_b0[1])));
}

static inline int alectov1_Rain_channel(int *cells_b0) {
  return (((cells_b0[1]) & 0xc) >> 2);
}

static inline int alectov1_Rain_battery_ok(int *cells_b1) {
  return ((((cells_b1[1]) & 0x80) >> 7) == 0);
}

static inline float alectov1_Rain_rain_mm(int *cells_b2, int *cells_b3) {
  return ((((reverse8((cells_b3[1]))) << 8) | (reverse8((cells_b2[1])))) * 0.25);
}

static inline bool alectov1_Temperature_validate_humidity(int *cells_b3) {
  return (((((reverse8((cells_b3[1]))) >> 4) * 0xa) + ((reverse8((cells_b3[1]))) & 0xf)) <= 0x64);
}

static inline int alectov1_Temperature_sensor_id(int *cells_b0) {
  return (reverse8((cells_b0[1])));
}

static inline int alectov1_Temperature_channel(int *cells_b0) {
  return (((cells_b0[1]) & 0xc) >> 2);
}

static inline int alectov1_Temperature_battery_ok(int *cells_b1) {
  return ((((cells_b1[1]) & 0x80) >> 7) == 0);
}

static inline float alectov1_Temperature_temperature_C(int *cells_b1, int *cells_b2) {
  return (((((reverse8((cells_b1[1]))) & 0xf0) | ((reverse8((cells_b2[1]))) << 8)) >> 4) * 0.1);
}

static inline int alectov1_Temperature_humidity(int *cells_b3) {
  return ((((reverse8((cells_b3[1]))) >> 4) * 0xa) + ((reverse8((cells_b3[1]))) & 0xf));
}

static int alectov1_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  uint8_t *b = bitbuffer->bb[0];
  unsigned bit_pos = 0;
  if (bitbuffer->bits_per_row[1] != 36)
      return DECODE_ABORT_LENGTH;
  int cells_b0[BITBUF_ROWS];
  int cells_b1[BITBUF_ROWS];
  int cells_b2[BITBUF_ROWS];
  int cells_b3[BITBUF_ROWS];
  int cells_b4[BITBUF_ROWS];
  static uint16_t const _rows_cells[] = {1, 2, 3, 4, 5, 6};
  for (size_t _k = 0; _k < 6; ++_k) {
    unsigned _r = _rows_cells[_k];
    uint8_t *b = bitbuffer->bb[_r];
    unsigned bit_pos = 0;
    cells_b0[_r] = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;
    cells_b1[_r] = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;
    cells_b2[_r] = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;
    cells_b3[_r] = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;
    cells_b4[_r] = bitrow_get_bits(b, bit_pos, 4);
    bit_pos += 4;
  }
  if (!alectov1_validate_packet(bitbuffer))
      return DECODE_FAIL_SANITY;
  if ((((((((cells_b1[1]) & 0x60) >> 5) == 3) & (((cells_b1[1]) & 0xf) != 0xc)) & (((cells_b1[1]) & 0xe) == 8)) & ((cells_b2[1]) == 0))) {
    data_t *data = data_make(
            "model", "", DATA_STRING, "AlectoV1-Wind",
            "id", "House Code", DATA_INT, alectov1_Wind0_sensor_id(cells_b0),
            "channel", "Channel", DATA_INT, alectov1_Wind0_channel(cells_b0),
            "battery_ok", "Battery", DATA_INT, alectov1_Wind0_battery_ok(cells_b1),
            "wind_avg_m_s", "Wind speed", DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, (double)alectov1_Wind0_wind_avg_m_s(cells_b3),
            "wind_max_m_s", "Wind gust", DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, (double)alectov1_Wind0_wind_max_m_s(cells_b3),
            "wind_dir_deg", "Wind Direction", DATA_INT, alectov1_Wind0_wind_dir_deg(cells_b1, cells_b2),
            "mic", "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
  } else if (((((((cells_b1[1]) & 0x60) >> 5) == 3) & (((cells_b1[1]) & 0xf) != 0xc)) & (((cells_b1[1]) & 0xe) == 0xe))) {
    data_t *data = data_make(
            "model", "", DATA_STRING, "AlectoV1-Wind",
            "id", "House Code", DATA_INT, alectov1_Wind4_sensor_id(cells_b0),
            "channel", "Channel", DATA_INT, alectov1_Wind4_channel(cells_b0),
            "battery_ok", "Battery", DATA_INT, alectov1_Wind4_battery_ok(cells_b1),
            "wind_avg_m_s", "Wind speed", DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, (double)alectov1_Wind4_wind_avg_m_s(cells_b3),
            "wind_max_m_s", "Wind gust", DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, (double)alectov1_Wind4_wind_max_m_s(),
            "wind_dir_deg", "Wind Direction", DATA_INT, alectov1_Wind4_wind_dir_deg(),
            "mic", "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
  } else if ((((((cells_b1[1]) & 0x60) >> 5) == 3) & (((cells_b1[1]) & 0xf) == 0xc))) {
    data_t *data = data_make(
            "model", "", DATA_STRING, "AlectoV1-Rain",
            "id", "House Code", DATA_INT, alectov1_Rain_sensor_id(cells_b0),
            "channel", "Channel", DATA_INT, alectov1_Rain_channel(cells_b0),
            "battery_ok", "Battery", DATA_INT, alectov1_Rain_battery_ok(cells_b1),
            "rain_mm", "Total Rain", DATA_FORMAT, "%.2f mm", DATA_DOUBLE, (double)alectov1_Rain_rain_mm(cells_b2, cells_b3),
            "mic", "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
  } else if (((((((((cells_b1[1]) & 0x60) >> 5) != 3) & ((cells_b0[2]) == (cells_b0[3]))) & ((cells_b0[3]) == (cells_b0[4]))) & ((cells_b0[4]) == (cells_b0[5]))) & ((cells_b0[5]) == (cells_b0[6])))) {
    if (!alectov1_Temperature_validate_humidity(cells_b3))
        return DECODE_FAIL_SANITY;
    data_t *data = data_make(
            "model", "", DATA_STRING, "AlectoV1-Temperature",
            "id", "House Code", DATA_INT, alectov1_Temperature_sensor_id(cells_b0),
            "channel", "Channel", DATA_INT, alectov1_Temperature_channel(cells_b0),
            "battery_ok", "Battery", DATA_INT, alectov1_Temperature_battery_ok(cells_b1),
            "temperature_C", "Temperature", DATA_FORMAT, "%.2f C", DATA_DOUBLE, (double)alectov1_Temperature_temperature_C(cells_b1, cells_b2),
            "humidity", "Humidity", DATA_FORMAT, "%u %%", DATA_INT, alectov1_Temperature_humidity(cells_b3),
            "mic", "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);
    return 1;
  }
  return DECODE_FAIL_SANITY;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "channel",
    "battery_ok",
    "wind_avg_m_s",
    "wind_max_m_s",
    "wind_dir_deg",
    "mic",
    "rain_mm",
    "temperature_C",
    "humidity",
    NULL,
};

r_device const alectov1 = {
    .name        = "AlectoV1 Weather Sensor (Alecto WS3500 WS4500 Ventus W155/W044 Oregon)",
    .modulation  = OOK_PULSE_PPM,
    .short_width = 2000.0,
    .long_width  = 4000.0,
    .reset_limit = 10000.0,
    .gap_limit   = 7000.0,
    .decode_fn   = &alectov1_decode,
    .fields      = output_fields,
};
