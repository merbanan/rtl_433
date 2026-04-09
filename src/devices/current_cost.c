// Generated from current_cost.py
/** @file
    CurrentCost TX, CurrentCost EnviR current sensors.

    Two framing variants (EnviR and Classic) with Manchester encoding.
    The EnviR transmits a 4-byte preamble (0x55555555) and 2-byte syncword (0x2DD4).
    The Classic uses a different init pattern (0xCCCCCCCE915D).

    Both variants decode a 4-bit message type and 12-bit device ID, followed
    by either three channel power readings (msg_type 0) or a counter/impulse
    reading (msg_type 4).
*/

#include "decoder.h"

/** @fn static int current_cost_envir_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    CurrentCost TX, CurrentCost EnviR current sensors.

    Two framing variants (EnviR and Classic) with Manchester encoding.
    The EnviR transmits a 4-byte preamble (0x55555555) and 2-byte syncword (0x2DD4).
    The Classic uses a different init pattern (0xCCCCCCCE915D).

    Both variants decode a 4-bit message type and 12-bit device ID, followed
    by either three channel power readings (msg_type 0) or a counter/impulse
    reading (msg_type 4).
*/
static int current_cost_envir_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    unsigned search_row = 0;
    uint8_t const preamble[] = {0x55, 0x55, 0x55, 0x55, 0xa4, 0x57};
    if (bitbuffer->num_rows < 1)
        return DECODE_ABORT_LENGTH;
    unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 48);
    if (offset >= bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;
    offset += 47;

    bitbuffer_t packet_bits = {0};
    bitbuffer_manchester_decode(bitbuffer, search_row, offset, &packet_bits, 0);

    if (packet_bits.bits_per_row[0] < 8)
        return DECODE_ABORT_LENGTH;
    uint8_t *b = packet_bits.bb[0];

    int msg_type = bitrow_get_bits(b, 0, 4);
    int device_id = bitrow_get_bits(b, 4, 12);

    if (msg_type == 0) {
        int ch0_valid = bitrow_get_bits(b, 16, 1);
        int ch0_power = bitrow_get_bits(b, 17, 15);
        int power0_W = (ch0_valid * ch0_power);
        int ch1_valid = bitrow_get_bits(b, 32, 1);
        int ch1_power = bitrow_get_bits(b, 33, 15);
        int power1_W = (ch1_valid * ch1_power);
        int ch2_valid = bitrow_get_bits(b, 48, 1);
        int ch2_power = bitrow_get_bits(b, 49, 15);
        int power2_W = (ch2_valid * ch2_power);

        /* clang-format off */
        data_t *data = data_make(
            "model", "", DATA_STRING, "CurrentCost-EnviR",
            "id", "Device Id", DATA_INT, device_id,
            "power0_W", "Power 0", DATA_INT, power0_W,
            "power1_W", "Power 1", DATA_INT, power1_W,
            "power2_W", "Power 2", DATA_INT, power2_W,
            NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    } else if (msg_type == 4) {
        int sensor_type = bitrow_get_bits(b, 24, 8);
        int impulse = bitrow_get_bits(b, 32, 32);

        /* clang-format off */
        data_t *data = data_make(
            "model", "", DATA_STRING, "CurrentCost-EnviRCounter",
            "subtype", "Sensor Id", DATA_INT, sensor_type,
            "id", "Device Id", DATA_INT, device_id,
            "power0", "Counter", DATA_INT, impulse,
            NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_FAIL_SANITY;

}

/** @fn static int current_cost_classic_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    CurrentCost TX, CurrentCost EnviR current sensors.

    Two framing variants (EnviR and Classic) with Manchester encoding.
    The EnviR transmits a 4-byte preamble (0x55555555) and 2-byte syncword (0x2DD4).
    The Classic uses a different init pattern (0xCCCCCCCE915D).

    Both variants decode a 4-bit message type and 12-bit device ID, followed
    by either three channel power readings (msg_type 0) or a counter/impulse
    reading (msg_type 4).
*/
static int current_cost_classic_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    unsigned search_row = 0;
    uint8_t const preamble[] = {0xcc, 0xcc, 0xcc, 0xce, 0x91, 0x5d};
    if (bitbuffer->num_rows < 1)
        return DECODE_ABORT_LENGTH;
    unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 45);
    if (offset >= bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;
    offset += 45;

    bitbuffer_t packet_bits = {0};
    bitbuffer_manchester_decode(bitbuffer, search_row, offset, &packet_bits, 0);

    if (packet_bits.bits_per_row[0] < 8)
        return DECODE_ABORT_LENGTH;
    uint8_t *b = packet_bits.bb[0];

    int msg_type = bitrow_get_bits(b, 0, 4);
    int device_id = bitrow_get_bits(b, 4, 12);

    if (msg_type == 0) {
        int ch0_valid = bitrow_get_bits(b, 16, 1);
        int ch0_power = bitrow_get_bits(b, 17, 15);
        int power0_W = (ch0_valid * ch0_power);
        int ch1_valid = bitrow_get_bits(b, 32, 1);
        int ch1_power = bitrow_get_bits(b, 33, 15);
        int power1_W = (ch1_valid * ch1_power);
        int ch2_valid = bitrow_get_bits(b, 48, 1);
        int ch2_power = bitrow_get_bits(b, 49, 15);
        int power2_W = (ch2_valid * ch2_power);

        /* clang-format off */
        data_t *data = data_make(
            "model", "", DATA_STRING, "CurrentCost-TX",
            "id", "Device Id", DATA_INT, device_id,
            "power0_W", "Power 0", DATA_INT, power0_W,
            "power1_W", "Power 1", DATA_INT, power1_W,
            "power2_W", "Power 2", DATA_INT, power2_W,
            NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    } else if (msg_type == 4) {
        int sensor_type = bitrow_get_bits(b, 24, 8);
        int impulse = bitrow_get_bits(b, 32, 32);

        /* clang-format off */
        data_t *data = data_make(
            "model", "", DATA_STRING, "CurrentCost-Counter",
            "subtype", "Sensor Id", DATA_INT, sensor_type,
            "id", "Device Id", DATA_INT, device_id,
            "power0", "Counter", DATA_INT, impulse,
            NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }

    return DECODE_FAIL_SANITY;

}

/** @fn static int current_cost_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    CurrentCost TX, CurrentCost EnviR current sensors.

    Two framing variants (EnviR and Classic) with Manchester encoding.
    The EnviR transmits a 4-byte preamble (0x55555555) and 2-byte syncword (0x2DD4).
    The Classic uses a different init pattern (0xCCCCCCCE915D).

    Both variants decode a 4-bit message type and 12-bit device ID, followed
    by either three channel power readings (msg_type 0) or a counter/impulse
    reading (msg_type 4).
    @sa current_cost_envir_decode()
    @sa current_cost_classic_decode()
*/
static int current_cost_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = DECODE_ABORT_EARLY;
    ret = current_cost_envir_decode(decoder, bitbuffer);
    if (ret > 0)
        return ret;
    bitbuffer_invert(bitbuffer);
    ret = current_cost_classic_decode(decoder, bitbuffer);
    if (ret > 0)
        return ret;
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
