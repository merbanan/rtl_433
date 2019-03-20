/** @file
    Simple FS20 remote decoder.

    Copyright (C) 2019 Dominik Pusch <dominik.pusch@koeln.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 3 as
    published by the Free Software Foundation.
*/

/*
Simple FS20 remote decoder.

Frequency: use rtl_433 -f 868.35M

fs20 protocol frame info from http://www.fhz4linux.info/tiki-index.php?page=FS20+Protocol

    preamble  hc1    parity  hc2    parity  address  parity  cmd    parity  chksum  parity  eot
    13 bit    8 bit  1 bit   8 bit  1 bit   8 bit    1 bit   8 bit  1 bit   8 bit   1 bit   1 bit

with extended commands

    preamble  hc1    parity  hc2    parity  address  parity  cmd    parity  ext    parity  chksum  parity  eot
    13 bit    8 bit  1 bit   8 bit  1 bit   8 bit    1 bit   8 bit  1 bit   8 bit  1 bit   8 bit   1 bit   1 bit

checksum and parity are not checked by this decoder.
Command extensions are also not decoded. feel free to improve!
*/

#include "decoder.h"

static int fs20_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    static char const *cmd_tab[] = {
            "off",
            "on, 6.25%",
            "on, 12.5%",
            "on, 18.75%",
            "on, 25%",
            "on, 31.25%",
            "on, 37.5%",
            "on, 43.75%",
            "on, 50%",
            "on, 56.25%",
            "on, 62.5%",
            "on, 68.75%",
            "on, 75%",
            "on, 81.25%",
            "on, 87.5%",
            "on, 93.75%",
            "on, 100%",
            "on, last value",
            "toggle on/off",
            "dim up",
            "dim down",
            "dim up/down",
            "set timer",
            "status request",
            "off, timer",
            "on, timer",
            "last value, timer",
            "reset to default",
            "unused",
            "unused",
            "unused",
            "unused",
    };

    uint8_t *b; // bits of a row
    uint8_t cmd;
    uint8_t hc1;
    uint8_t hc2;
    uint16_t hc;
    uint8_t address;
    data_t *data;
    uint16_t ad_b4 = 0;
    uint32_t hc_b4 = 0;

    // check length of frame
    if (bitbuffer->bits_per_row[0] != 58) {
        // check extended length (never tested!)
        if (bitbuffer->bits_per_row[0] != 67)
            return 0;
    }

    b = bitbuffer->bb[0];
    // check preamble first 13 bits '0000 0000 0000 1'
    if ((b[0] != 0x00) || ((b[1] & 0xf8) != 0x08))
        return 0;

    // parse values from buffer
    hc1     = (b[1] << 5) | (b[2] >> 3);
    hc2     = (b[2] << 6) | (b[3] >> 2);
    hc      = hc1 << 8 | hc2;
    address = (b[3] << 7) | (b[4] >> 1);
    cmd     = b[5] & 0x1f;

    // convert address to fs20 format (base4+1)
    for (uint8_t i = 0; i < 4; i++) {
        ad_b4 += (address % 4 + 1) << i * 4;
        address /= 4;
    }

    // convert housecode to fs20 format (base4+1)
    for (uint8_t i = 0; i < 8; i++) {
        hc_b4 += ((hc % 4) + 1) << i * 4;
        hc /= 4;
    }

    /* clang-format off */
    data = data_make(
            "model",        "", DATA_STRING, "FS20",
            "housecode",    "", DATA_FORMAT, "%x", DATA_INT, hc_b4,
            "address",      "", DATA_FORMAT, "%x", DATA_INT, ad_b4,
            "command",      "", DATA_STRING, cmd_tab[cmd],
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "housecode",
        "address",
        "command",
        NULL,
};

r_device fs20 = {
        .name        = "FS20",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 400,
        .long_width  = 600,
        .reset_limit = 9000,
        .decode_fn   = &fs20_decode,
        .disabled    = 1, // missing MIC and no sample data
        .fields      = output_fields,
};
