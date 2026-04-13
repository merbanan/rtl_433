// Generated from honeywell_wdb.py
/** @file
    Honeywell ActivLink, Wireless Doorbell decoder.
*/

#include <stdbool.h>

#include "decoder.h"
#include "honeywell_wdb.h"

static inline int honeywell_wdb_battery_ok(int battery_low) {
  return (battery_low == 0);
}

/**
Honeywell ActivLink, Wireless Doorbell decoder.
*/
static int honeywell_wdb_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
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
  if (!honeywell_wdb_validate_packet(bitbuffer))
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
          "model", "", DATA_STRING, "Honeywell-ActivLink",
          "subtype", "Class", DATA_STRING, honeywell_wdb_subtype(class_raw),
          "id", "Id", DATA_FORMAT, "%x", DATA_INT, device,
          "battery_ok", "Battery", DATA_INT, honeywell_wdb_battery_ok(battery_low),
          "alert", "Alert", DATA_STRING, honeywell_wdb_alert(alert_raw),
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

r_device const honeywell_wdb = {
    .name        = "Honeywell ActivLink, Wireless Doorbell",
    .modulation  = OOK_PULSE_PWM,
    .short_width = 175.0,
    .long_width  = 340.0,
    .reset_limit = 5000.0,
    .gap_limit   = 0.0,
    .sync_width  = 500.0,
    .decode_fn   = &honeywell_wdb_decode,
    .fields      = output_fields,
};
