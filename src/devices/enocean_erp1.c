/** @file
    EnOcean ERP1

    Copyright (C) 2021 Christoph M. Wintersteiger <christoph@winterstiger.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
EnOcean Radio Protocol 1

868.3Mhz ASK, 125kbps, inverted, 8/12 coding
Spec: https://www.enocean.com/erp1/
*/

#include "decoder.h"

static int decode_8of12(uint8_t const *b, int pos, int end, bitbuffer_t *out)
{
    if (pos + 12 > end)
        return DECODE_ABORT_LENGTH;

    bitbuffer_add_bit(out, bitrow_get_bit(b, pos + 0));
    bitbuffer_add_bit(out, bitrow_get_bit(b, pos + 1));
    uint8_t b2 = bitrow_get_bit(b, pos + 2);
    bitbuffer_add_bit(out, b2);

    if (b2 != !bitrow_get_bit(b, pos + 3))
        return DECODE_FAIL_SANITY;

    bitbuffer_add_bit(out, bitrow_get_bit(b, pos + 4));
    bitbuffer_add_bit(out, bitrow_get_bit(b, pos + 5));
    uint8_t b6 = bitrow_get_bit(b, pos + 6);
    bitbuffer_add_bit(out, b6);

    if (b6 != !bitrow_get_bit(b, pos + 7))
        return DECODE_FAIL_SANITY;

    bitbuffer_add_bit(out, bitrow_get_bit(b, pos + 8));
    bitbuffer_add_bit(out, bitrow_get_bit(b, pos + 9));

    return (bitrow_get_bit(b, pos + 10) << 1) | bitrow_get_bit(b, pos + 11);
}

static int enocean_erp1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1)
        return DECODE_ABORT_EARLY;

    bitbuffer_invert(bitbuffer);

    uint8_t preamble[2] = { 0x55, 0x20 };
    unsigned start = bitbuffer_search(bitbuffer, 0, 0, preamble, 11);
    if (start >= bitbuffer->bits_per_row[0])
        return DECODE_FAIL_SANITY;

    unsigned pos = start + 11;
    unsigned len = bitbuffer->bits_per_row[0] - start;
    unsigned end = pos + len;

    bitbuffer_t bytes = {0};
    uint8_t more = 0x01;
    do {
        more = decode_8of12(bitbuffer->bb[0], pos, end, &bytes);
        pos += 12;
    } while (pos < end && more == 0x01);

    if (bytes.bits_per_row[0] < 16)
        return DECODE_ABORT_LENGTH;

    uint8_t chk = crc8(bytes.bb[0], (bytes.bits_per_row[0] - 1) / 8, 0x07, 0x00);
    if (chk != bitrow_get_byte(bytes.bb[0], bytes.bits_per_row[0] - 8))
        return DECODE_FAIL_MIC;

    /* clang-format off */
    data_t *data = data_make(
            "model",    "",             DATA_STRING, "EnOcean ERP1",
            "mic",      "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    uint8_t* buf = bytes.bb[0];
    size_t buf_sz = bytes.bits_per_row[0] / 8;
    if (buf && buf_sz > 0)  {
        char tstr[256];
        bitrow_snprint(buf, buf_sz * 8, tstr, sizeof (tstr));
        data = data_append(data, "telegram", "", DATA_STRING, tstr, NULL);
    }

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "telegram",
        "mic",
        NULL,
};

r_device enocean_erp1 = {
        .name        = "EnOcean ERP1",
        .modulation  = OOK_PULSE_PCM_RZ,
        .short_width = 8,
        .long_width  = 8,
        .sync_width  = 0,
        .tolerance   = 1,
        .reset_limit = 800,
        .decode_fn   = &enocean_erp1_decode,
        .fields      = output_fields,
};
