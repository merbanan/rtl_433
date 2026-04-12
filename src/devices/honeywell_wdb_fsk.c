// Generated from honeywell_wdb_fsk.py
/** @file
    Honeywell ActivLink, Wireless Doorbell (FSK) decoder.
*/

#include "decoder.h"
#include "honeywell_wdb_fsk.h"

static inline int honeywell_wdb_fsk_battery_ok(int battery_low) {
  return (battery_low == 0);
}

static int honeywell_wdb_fsk_decode(r_device *decoder, bitbuffer_t *bitbuffer) {
  int row = bitbuffer_find_repeated_row(bitbuffer, 4, 48);
  if (row < 0)
      return DECODE_ABORT_EARLY;
  if (bitbuffer->bits_per_row[row] != 48)
      return DECODE_ABORT_LENGTH;
  bitbuffer_invert(bitbuffer);
  uint8_t *b = bitbuffer->bb[row];
  unsigned bit_pos = 0;
  int device = bitrow_get_bits(b, bit_pos, 20);
  bit_pos += 20;
  if (!honeywell_wdb_fsk_validate_packet(bitbuffer))
      return DECODE_FAIL_SANITY;
  bit_pos += 6;
  int class_raw = bitrow_get_bits(b, bit_pos, 2);
  bit_pos += 2;
  bit_pos += 10;
  int alert_raw = bitrow_get_bits(b, bit_pos, 2);
  bit_pos += 2;
  bit_pos += 3;
  int secret_knock = bitrow_get_bits(b, bit_pos, 1);
  bit_pos += 1;
  int relay = bitrow_get_bits(b, bit_pos, 1);
  bit_pos += 1;
  bit_pos += 1;
  int battery_low = bitrow_get_bits(b, bit_pos, 1);
  bit_pos += 1;
  bit_pos += 1;
  data_t *data = data_make(
          "model", "", DATA_STRING, "Honeywell-ActivLinkFSK",
          "subtype", "Class", DATA_STRING, honeywell_wdb_fsk_subtype(class_raw),
          "id", "Id", DATA_FORMAT, "%x", DATA_INT, device,
          "battery_ok", "Battery", DATA_INT, honeywell_wdb_fsk_battery_ok(battery_low),
          "alert", "Alert", DATA_STRING, honeywell_wdb_fsk_alert(alert_raw),
          "secret_knock", "Secret Knock", DATA_FORMAT, "%d", DATA_INT, secret_knock,
          "relay", "Relay", DATA_FORMAT, "%d", DATA_INT, relay,
          "mic", "Integrity", DATA_STRING, "PARITY",
          NULL);
  decoder_output_data(decoder, data);
  return 1;
}

static char const *const output_fields[] = {
    "model",
    "subtype",
    "id",
    "battery_ok",
    "alert",
    "secret_knock",
    "relay",
    "mic",
    NULL,
};

r_device const honeywell_wdb_fsk = {
    .name        = "Honeywell ActivLink, Wireless Doorbell (FSK)",
    .modulation  = FSK_PULSE_PWM,
    .short_width = 160.0,
    .long_width  = 320.0,
    .reset_limit = 560.0,
    .gap_limit   = 0.0,
    .sync_width  = 500.0,
    .decode_fn   = &honeywell_wdb_fsk_decode,
    .fields      = output_fields,
};
