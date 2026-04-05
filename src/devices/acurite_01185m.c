// Generated from acurite_01185m.py
/** @file
    Decoder for Acurite Grill/Meat Thermometer 01185M.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
    Based on work by Joe "exeljb"

    Modulation:

    - 56 bit PWM data
    - short is 840 us pulse, 2028 us gap
    - long is 2070 us pulse, 800 us gap,
    - sync is 6600 us pulse, 4080 gap,
    - there is no packet gap and 8 repeats
    - data is inverted (short=0, long=1) and byte-reflected

    S.a. #1824

    Temperature is 16 bit, degrees F, scaled x10 +900.
    The first reading is the "Meat" channel and the second is for the "Ambient" or
    grill temperature.

    - A value of 0x1b58 (7000 / 610F) indicates the sensor is unplugged (E1).
    - A value of 0x00c8 (200 / -70F) indicates a sensor problem (E2).

    The battery status is the MSB of the second byte, 0 for good battery, 1 for low.

    Channel appears random (often 3, 6, 12, 15).

    Data layout (56 bits):

        II BC MM MM TT TT XX

    - I: 8 bit ID
    - B: 1 bit battery-low (MSB of second byte); middle bits skipped below
    - C: 4 bit channel (low nibble of second byte)
    - M: 16 bit temperature 1 (F x10 +900)
    - T: 16 bit temperature 2
    - X: 8 bit checksum, add-with-carry over first 6 bytes
*/

#include "decoder.h"

/** @fn static int acurite_01185m_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    Decoder for Acurite Grill/Meat Thermometer 01185M.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
    Based on work by Joe "exeljb"

    Modulation:

    - 56 bit PWM data
    - short is 840 us pulse, 2028 us gap
    - long is 2070 us pulse, 800 us gap,
    - sync is 6600 us pulse, 4080 gap,
    - there is no packet gap and 8 repeats
    - data is inverted (short=0, long=1) and byte-reflected

    S.a. #1824

    Temperature is 16 bit, degrees F, scaled x10 +900.
    The first reading is the "Meat" channel and the second is for the "Ambient" or
    grill temperature.

    - A value of 0x1b58 (7000 / 610F) indicates the sensor is unplugged (E1).
    - A value of 0x00c8 (200 / -70F) indicates a sensor problem (E2).

    The battery status is the MSB of the second byte, 0 for good battery, 1 for low.

    Channel appears random (often 3, 6, 12, 15).

    Data layout (56 bits):

        II BC MM MM TT TT XX

    - I: 8 bit ID
    - B: 1 bit battery-low (MSB of second byte); middle bits skipped below
    - C: 4 bit channel (low nibble of second byte)
    - M: 16 bit temperature 1 (F x10 +900)
    - T: 16 bit temperature 2
    - X: 8 bit checksum, add-with-carry over first 6 bytes
*/
static int acurite_01185m_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int result = 0;
    bitbuffer_invert(bitbuffer);

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] != 56) {
            result = DECODE_ABORT_LENGTH;
            continue;
        }
        uint8_t *b = bitbuffer->bb[row];
        reflect_bytes(b, 7);

        int sum = add_bytes(b, 6);
        if ((sum & 0xff) != b[6]) {
            result = DECODE_FAIL_MIC;
            continue;
        }
        if (sum == 0) {
            return DECODE_FAIL_SANITY;
        }

        int id = bitrow_get_bits(b, 0, 8);
        int battery_low = bitrow_get_bits(b, 8, 1);
        (void)(bitrow_get_bits(b, 9, 3));
        int channel = bitrow_get_bits(b, 12, 4);
        int temp1_raw = bitrow_get_bits(b, 16, 16);
        int temp2_raw = bitrow_get_bits(b, 32, 16);
        (void)(bitrow_get_bits(b, 48, 8));

        int temp1_ok = ((temp1_raw > 0xc8) & (temp1_raw < 0x1b58));
        int temp2_ok = ((temp2_raw > 0xc8) & (temp2_raw < 0x1b58));
        float temp1_f = ((temp1_raw - 0x384) * 0.1);
        float temp2_f = ((temp2_raw - 0x384) * 0.1);
        int battery_ok = (battery_low == 0);

        /* clang-format off */
        data_t *data = data_make(
            "model", "", DATA_STRING, "Acurite-01185M",
            "id", "", DATA_INT, id,
            "channel", "", DATA_INT, channel,
            "battery_ok", "Battery", DATA_INT, battery_ok,
            "temperature_1_F", "Meat", DATA_COND, temp1_ok, DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp1_f,
            "temperature_2_F", "Ambient", DATA_COND, temp2_ok, DATA_FORMAT, "%.1f F", DATA_DOUBLE, temp2_f,
            "mic", "Integrity", DATA_STRING, "CHECKSUM",
            NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
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
