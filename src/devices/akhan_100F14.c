// Generated from akhan_100F14.py
/** @file
    Akhan 100F14 remote keyless entry.

    RKE using HS1527-style framing: preamble, 20-bit id, 4-bit command.
    Row is 25 bits; the last bit is consumed as padding after the 24-bit message.

    Reference: src/devices/akhan_100F14.c (hand-written; this module replaces it).
*/

#include "decoder.h"
#include "akhan_100F14.h"

/** @fn static int akhan_100F14_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    Akhan 100F14 remote keyless entry.

    RKE using HS1527-style framing: preamble, 20-bit id, 4-bit command.
    Row is 25 bits; the last bit is consumed as padding after the 24-bit message.

    Reference: src/devices/akhan_100F14.c (hand-written; this module replaces it).
*/
static int akhan_100F14_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 1, 25);
    if (row < 0)
        return DECODE_ABORT_EARLY;
    if (bitbuffer->bits_per_row[row] != 25)
        return DECODE_ABORT_LENGTH;

    unsigned offset = 0;

    uint8_t b[4];
    bitbuffer_extract_bytes(bitbuffer, row, offset, b, 25);

    int b0 = bitrow_get_bits(b, 0, 8);
    int b1 = bitrow_get_bits(b, 8, 8);
    int b2 = bitrow_get_bits(b, 16, 8);
    (void)(bitrow_get_bits(b, 24, 1));

    if (!((((((((~b2) & 0xff) & 0xf) == 1) | ((((~b2) & 0xff) & 0xf) == 2)) | ((((~b2) & 0xff) & 0xf) == 4)) | ((((~b2) & 0xff) & 0xf) == 8))))
        return DECODE_FAIL_SANITY;

    int notb0 = ((~b0) & 0xff);
    int notb1 = ((~b1) & 0xff);
    int notb2 = ((~b2) & 0xff);
    int id = (((notb0 << 0xc) | (notb1 << 4)) | (notb2 >> 4));
    int cmd = (notb2 & 0xf);
    const char * data_str = akhan_100F14_data_str(cmd);

    /* clang-format off */
    data_t *data = data_make(
        "model", "", DATA_STRING, "Akhan-100F14",
        "id", "ID (20bit)", DATA_FORMAT, "0x%x", DATA_INT, id,
        "data", "Data (4bit)", DATA_STRING, data_str,
        NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
    "model",
    "id",
    "data",
    NULL,
};

r_device const akhan_100F14 = {
    .name        = "Akhan 100F14 remote keyless entry",
    .modulation  = OOK_PULSE_PWM,
    .short_width = 316.0,
    .long_width  = 1020.0,
    .reset_limit = 1800.0,
    .sync_width  = 0.0,
    .tolerance   = 80.0,
    .decode_fn   = &akhan_100F14_decode,
    .fields      = output_fields,
    .disabled    = 1,
};
