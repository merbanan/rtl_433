// Generated from thermopro_tp211b.py
/** @file
    ThermoPro TP211B Thermometer.

    RF:
    - 915 MHz FSK temperature sensor.

    Flex decoder:

        rtl_433 -f 915M -X "n=tp211b,m=FSK_PCM,s=105,l=105,r=1500,preamble=552dd4"

    Data layout after preamble:

        Byte Position   0  1  2  3  4  5  6  7
        Sample          01 1e d6 03 6c aa 14 ff
        Sample          01 1e d6 02 fa aa c4 1e
                        II II II fT TT aa CC CC

    - III: {24} Sensor ID
    - f:   {4}  Flags or unused, always 0
    - TTT: {12} Temperature, raw value, deg C = (raw - 500) / 10
    - aa:  {8}  Fixed value 0xAA
    - CC:  {16} Checksum, XOR table based (see thermopro_tp211b.h).
*/

#include "decoder.h"
#include "thermopro_tp211b.h"

/** @fn static int thermopro_tp211b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    ThermoPro TP211B Thermometer.

    RF:
    - 915 MHz FSK temperature sensor.

    Flex decoder:

        rtl_433 -f 915M -X "n=tp211b,m=FSK_PCM,s=105,l=105,r=1500,preamble=552dd4"

    Data layout after preamble:

        Byte Position   0  1  2  3  4  5  6  7
        Sample          01 1e d6 03 6c aa 14 ff
        Sample          01 1e d6 02 fa aa c4 1e
                        II II II fT TT aa CC CC

    - III: {24} Sensor ID
    - f:   {4}  Flags or unused, always 0
    - TTT: {12} Temperature, raw value, deg C = (raw - 500) / 10
    - aa:  {8}  Fixed value 0xAA
    - CC:  {16} Checksum, XOR table based (see thermopro_tp211b.h).
*/
static int thermopro_tp211b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x55, 0x2d, 0xd4};
    if (bitbuffer->num_rows < 1)
        return DECODE_ABORT_LENGTH;
    unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 24);
    if (offset >= bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;
    offset += 24;

    uint8_t b[8];
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 64);

    int id = bitrow_get_bits(b, 0, 24);
    int flags = bitrow_get_bits(b, 24, 4);
    int temp_raw = bitrow_get_bits(b, 28, 12);
    if (bitrow_get_bits(b, 40, 8) != 0xaa)
        return DECODE_FAIL_SANITY;
    int checksum = bitrow_get_bits(b, 48, 16);

    int valid = thermopro_tp211b_validate(b, checksum);
    if (valid != 0)
        return valid;

    float temperature_c = ((temp_raw - 0x1f4) * 0.1);

    /* clang-format off */
    data_t *data = data_make(
        "model", "", DATA_STRING, "ThermoPro-TP211B",
        "id", "", DATA_INT, id,
        "flags", "", DATA_INT, flags,
        "temp_raw", "", DATA_INT, temp_raw,
        "checksum", "", DATA_INT, checksum,
        "temperature_c", "", DATA_DOUBLE, temperature_c,
        NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "flags",
    "temp_raw",
    "checksum",
    "temperature_c",
    NULL,
};

r_device const thermopro_tp211b = {
    .name        = "ThermoPro TP211B Thermometer",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 105.0,
    .long_width  = 105.0,
    .reset_limit = 1500.0,
    .decode_fn   = &thermopro_tp211b_decode,
    .fields      = output_fields,
};
