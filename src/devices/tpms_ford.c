// Generated from tpms_ford.py
/** @file
    FSK 8 byte Manchester encoded TPMS with simple checksum.

    Seen on Ford Fiesta, Focus, Kuga, Escape, Transit...

    Seen on 315.00 MHz (United States) and 433.92 MHz.
    Likely VDO-Sensors, Type "S180084730Z", built by "Continental Automotive GmbH".

    Packet nibbles:

        II II II II PP TT FF CC

    - I = ID
    - P = Pressure, as PSI * 4
    - T = Temperature, as C + 56
    - F = Flags
    - C = Checksum, SUM bytes 0 to 6 = byte 7
*/

#include "decoder.h"

/** @fn static int tpms_ford_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    FSK 8 byte Manchester encoded TPMS with simple checksum.

    Seen on Ford Fiesta, Focus, Kuga, Escape, Transit...

    Seen on 315.00 MHz (United States) and 433.92 MHz.
    Likely VDO-Sensors, Type "S180084730Z", built by "Continental Automotive GmbH".

    Packet nibbles:

        II II II II PP TT FF CC

    - I = ID
    - P = Pressure, as PSI * 4
    - T = Temperature, as C + 56
    - F = Flags
    - C = Checksum, SUM bytes 0 to 6 = byte 7
*/
static int tpms_ford_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    unsigned search_row = 0;
    uint8_t const preamble[] = {0xaa, 0xa9};
    if (bitbuffer->num_rows < 1)
        return DECODE_ABORT_LENGTH;
    unsigned offset = 0;
    int preamble_found = 0;
    for (unsigned row = 0; row < (unsigned)bitbuffer->num_rows && !preamble_found; ++row) {
        unsigned pos = bitbuffer_search(bitbuffer, row, 0, preamble, 16);
        if (pos < bitbuffer->bits_per_row[row]) {
            search_row = row;
            offset = pos;
            preamble_found = 1;
        }
    }
    if (!preamble_found)
        return DECODE_ABORT_EARLY;
    offset += 16;

    bitbuffer_t packet_bits = {0};
    bitbuffer_manchester_decode(bitbuffer, search_row, offset, &packet_bits, 160);

    if (packet_bits.bits_per_row[0] < 8)
        return DECODE_ABORT_LENGTH;
    uint8_t *b = packet_bits.bb[0];

    int sensor_id = bitrow_get_bits(b, 0, 32);
    int pressure_raw = bitrow_get_bits(b, 32, 8);
    int temp_byte = bitrow_get_bits(b, 40, 8);
    int flags = bitrow_get_bits(b, 48, 8);
    int checksum = bitrow_get_bits(b, 56, 8);

    if (!((((((((((sensor_id >> 0x18) + ((sensor_id >> 0x10) & 0xff)) + ((sensor_id >> 8) & 0xff)) + (sensor_id & 0xff)) + pressure_raw) + temp_byte) + flags) & 0xff) == checksum)))
        return DECODE_FAIL_SANITY;

    float pressure_psi = ((((flags & 0x20) << 3) | pressure_raw) * 0.25);
    float temperature_c = ((temp_byte & 0x7f) - 0x38);
    int moving = ((flags & 0x44) == 0x44);
    int learn = ((flags & 0x4c) == 8);
    int code = (((pressure_raw << 0x10) | (temp_byte << 8)) | flags);
    int unknown = (flags & 0x90);
    int unknown_3 = (flags & 3);

    char json_str_2[64];
    snprintf(json_str_2, sizeof(json_str_2), "%08x", sensor_id);
    char json_str_6[64];
    snprintf(json_str_6, sizeof(json_str_6), "%06x", code);
    char json_str_7[64];
    snprintf(json_str_7, sizeof(json_str_7), "%02x", unknown);
    char json_str_8[64];
    snprintf(json_str_8, sizeof(json_str_8), "%01x", unknown_3);

    /* clang-format off */
    data_t *data = data_make(
        "model", "", DATA_STRING, "Ford",
        "type", "", DATA_STRING, "TPMS",
        "id", "", DATA_FORMAT, "%08x", DATA_STRING, json_str_2,
        "pressure_PSI", "Pressure", DATA_DOUBLE, pressure_psi,
        "moving", "Moving", DATA_INT, moving,
        "learn", "Learn", DATA_INT, learn,
        "code", "", DATA_FORMAT, "%06x", DATA_STRING, json_str_6,
        "unknown", "", DATA_FORMAT, "%02x", DATA_STRING, json_str_7,
        "unknown_3", "", DATA_FORMAT, "%01x", DATA_STRING, json_str_8,
        "mic", "Integrity", DATA_STRING, "CHECKSUM",
        NULL);
    /* clang-format on */
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
