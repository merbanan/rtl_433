// Generated from honeywell_wdb.py
/** @file
    Honeywell ActivLink, wireless door bell, PIR Motion sensor (OOK).
*/

#include "decoder.h"
#include "honeywell_wdb.h"

/** @fn static int honeywell_wdb_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    Honeywell ActivLink, wireless door bell, PIR Motion sensor (OOK).
*/
static int honeywell_wdb_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 4, 48);
    if (row < 0)
        return DECODE_ABORT_EARLY;
    if (bitbuffer->bits_per_row[row] != 48)
        return DECODE_ABORT_LENGTH;

    bitbuffer_invert(bitbuffer);

    unsigned offset = 0;

    uint8_t b[6];
    bitbuffer_extract_bytes(bitbuffer, row, offset, b, 48);

    int vret_validate_packet = honeywell_wdb_validate_packet(b);
    if (vret_validate_packet != 0)
        return vret_validate_packet;

    int device = bitrow_get_bits(b, 0, 20);
    (void)(bitrow_get_bits(b, 20, 6));
    int class_raw = bitrow_get_bits(b, 26, 2);
    (void)(bitrow_get_bits(b, 28, 10));
    int alert_raw = bitrow_get_bits(b, 38, 2);
    (void)(bitrow_get_bits(b, 40, 3));
    int secret_knock = bitrow_get_bits(b, 43, 1);
    int relay = bitrow_get_bits(b, 44, 1);
    (void)(bitrow_get_bits(b, 45, 1));
    int battery_low = bitrow_get_bits(b, 46, 1);
    int battery_ok = (battery_low == 0);
    (void)(bitrow_get_bits(b, 47, 1));

    const char * subtype = honeywell_wdb_subtype(class_raw);
    const char * alert = honeywell_wdb_alert(alert_raw);

    /* clang-format off */
    data_t *data = data_make(
        "model", "", DATA_STRING, "Honeywell-ActivLink",
        "subtype", "Class", DATA_STRING, subtype,
        "id", "Id", DATA_FORMAT, "%x", DATA_INT, device,
        "battery_ok", "Battery", DATA_INT, battery_ok,
        "alert", "Alert", DATA_STRING, alert,
        "secret_knock", "Secret Knock", DATA_FORMAT, "%d", DATA_INT, secret_knock,
        "relay", "Relay", DATA_FORMAT, "%d", DATA_INT, relay,
        "mic", "Integrity", DATA_STRING, "PARITY",
        NULL);
    /* clang-format on */
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
