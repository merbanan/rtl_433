/** @file
    Kaku decoder.
*/
/**
Kaku decoder.
Might be similar to an x1527.
S.a. Nexa, Proove.

Two bits map to 2 states, 0 1 -> 0 and 1 0 -> 1
Status bit can be 1 1 -> 1 which indicates DIM value. 4 extra bits are present with value
start pulse: 1T high, 10.44T low
- 26 bit:  Address
- 1  bit:  group bit
- 1  bit:  Status bit on/off/[dim]
- 4  bit:  unit
- [4 bit:  dim level. Present if [dim] is used, but might be present anyway...]
- stop pulse: 1T high, 40T low
*/

#include "decoder.h"

static int newkaku_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    bitrow_t *bb = bitbuffer->bb;

    if (bb[0][0] != 0x65 && bb[0][0] != 0x59) // always starts with 0110 0101 or 0101 1001
        return DECODE_ABORT_EARLY;

    /* Reject missing sync */
    if (bitbuffer->syncs_before_row[0] != 1)
        return DECODE_ABORT_EARLY;

    /* Reject codes of wrong length */
    if (bitbuffer->bits_per_row[0] != 64 && bitbuffer->bits_per_row[0] != 72)
        return DECODE_ABORT_LENGTH;

    // 11 for command indicates DIM, 4 extra bits indicate DIM value
    uint8_t dim_cmd = (bb[0][6] & 0x03) == 0x03;
    if (dim_cmd) {
        bb[0][6] &= 0xfe; // change DIM to ON to use Manchester
    }

    bitbuffer_t databits = {0};
    // note: not manchester encoded but actually ternary
    unsigned pos = bitbuffer_manchester_decode(bitbuffer, 0, 0, &databits, 80);
    bitbuffer_invert(&databits);

    /* Reject codes when Manchester decoding fails */
    if (pos != 64 && pos != 72)
        return DECODE_ABORT_LENGTH;

    uint8_t *b = databits.bb[0];

    uint32_t id        = (b[0] << 18) | (b[1] << 10) | (b[2] << 2) | (b[3] >> 6); // ID 26 bits
    uint32_t group_cmd = (b[3] >> 5) & 1;
    uint32_t on_bit    = (b[3] >> 4) & 1;
    uint32_t unit      = (b[3] & 0x0f);
    uint32_t dv        = (b[4] >> 4);

    /* clang-format off */
    data = data_make(
            "model",        "",             DATA_STRING, _X("KlikAanKlikUit-Switch","KlikAanKlikUit Wireless Switch"),
            "id",           "",             DATA_INT,    id,
            "unit",         "Unit",         DATA_INT,    unit,
            "group_call",   "Group Call",   DATA_STRING, group_cmd ? "Yes" : "No",
            "command",      "Command",      DATA_STRING, on_bit ? "On" : "Off",
            "dim",          "Dim",          DATA_STRING, dim_cmd ? "Yes" : "No",
            "dim_value",    "Dim Value",    DATA_INT,    dv,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "unit",
        "group_call",
        "command",
        "dim",
        "dim_value",
        NULL,
};

r_device newkaku = {
        .name        = "KlikAanKlikUit Wireless Switch",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 300,  // 1:1
        .long_width  = 1400, // 1:5
        .sync_width  = 2700, // 1:10
        .tolerance   = 200,
        .reset_limit = 3200,
        .decode_fn   = &newkaku_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
