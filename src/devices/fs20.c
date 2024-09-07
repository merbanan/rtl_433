/** @file
    Simple FS20 remote decoder.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>
    original implementation 2019 Dominik Pusch <dominik.pusch@koeln.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int fs20_decode(r_device *decoder, bitbuffer_t *bitbuffer)
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

static int fs20_find_preamble(bitbuffer_t *bitbuffer, int bitpos)
{
    // Preamble is 12 x '0' | '1', but we ignore the first preamble bit
    // Last bit ('1') is at position (pattern[1] >> 4 & 1)
    uint8_t const preamble_pattern[2] = {0x00, 0x10};
    uint8_t const min_packet_length   = 4 * (8 + 1);

    // fast scan for 8 consecutive '0' bits
    uint8_t *bits = bitbuffer->bb[0];
    while ((bitpos + 12 + min_packet_length < bitbuffer->bits_per_row[0])
        && ((bits[(bitpos / 8) + 1] == 0) || (bits[(bitpos / 8)] != 0))) {
        bitpos += 8;
    }
    if (bitpos) {
        bitpos--;
        bitpos &= ~0x3;
    }

    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 12)) < bitbuffer->bits_per_row[0]) {
        if (bitpos + min_packet_length >= bitbuffer->bits_per_row[0]) {
            return DECODE_ABORT_LENGTH;
        }

        return bitpos + 12;
    }

    // preamble not found
    return DECODE_FAIL_SANITY;
}

struct parity_byte {
    uint8_t data;
    uint8_t err;
};

static struct parity_byte get_byte(uint8_t *bits, unsigned pos)
{
    uint16_t word = (bits[pos / 8] << 8) | bits[(pos / 8) + 1];
    struct parity_byte res;

    word <<= pos & 7;
    res.data = word >> 8;
    // parity8 returns odd parity, bit 9 is even parity
    res.err = parity8(res.data) != (word >> 7 & 1);

    return res;
}

static int fs20_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    static char const *const cmd_tab[] = {
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
    static char const *const flags_tab[8] = {
            "(none)",
            "Extended",
            "BiDir",
            "Extended | BiDir",
            "Response",
            "Response | Extended",
            "Response | BiDir",
            "Response | Extended | BiDir",
    };
    static char const *const fht_cmd_tab[16] = {
            "end-of-sync",
            "valve open",
            "valve close",
            "? (0x3)",
            "? (0x4)",
            "? (0x5)",
            "valve open <ext>%",
            "? (0x7)",
            "offset adjust",
            "? (0x9)",
            "valve de-scale",
            "? (0x11)",
            "sync countdown",
            "? (0x13)",
            "beep",
            "pairing?",
    };
    static char const *const fht_flags_tab[8] = {
            "(none)",
            "Extended",
            "BS?",
            "Extended | BS?",
            "Repeat",
            "Repeat | Extended",
            "Repeat | BS?",
            "Repeat | Extended | BS?",
    };

    bitbuffer_invert(bitbuffer);

    uint8_t *bits = bitbuffer->bb[0];
    uint8_t cmd;
    uint16_t hc;
    uint8_t address;
    uint8_t ext = 0;
    uint8_t sum;

    data_t *data;
    uint16_t ad_b4 = 0;
    uint32_t hc_b4 = 0;

    int rc     = DECODE_FAIL_MIC;
    int bitpos = 0;

    while ((bitpos = fs20_find_preamble(bitbuffer, bitpos)) >= 0) {
        decoder_logf(decoder, 2, __func__, "Found preamble at %d", bitpos);

        struct parity_byte res;

        res = get_byte(bits, bitpos);
        if (res.err)
            continue;
        hc = res.data << 8;

        res = get_byte(bits, bitpos + 9);
        if (res.err)
            continue;
        hc |= res.data;

        res = get_byte(bits, bitpos + 18);
        if (res.err)
            continue;
        address = res.data;

        res = get_byte(bits, bitpos + 27);
        if (res.err)
            continue;
        cmd = res.data;

        res = get_byte(bits, bitpos + 36);
        if (res.err)
            continue;

        if (cmd & 0x20) {
            ext = res.data;
            if (bitpos + 45 + 9 > bitbuffer->bits_per_row[0])
                break;

            res = get_byte(bits, bitpos + 45);
            if (res.err)
                continue;
        }
        sum = res.data;

        rc = 1;
        break;
    }

    // propagate MIC
    if (rc <= 0) {
        return rc;
    }

    if (bitpos < 0) {
        return bitpos;
    }

    // Sum is (HC1 + HC2 + Addr + Cmd [+ Ext] + Type + Repeater-Hopcount
    // Type is either 6 for regular FS20 devices (switches, dimmers, ...)
    // or 0xC for FHT (radiator valves)
    sum -= hc >> 8;
    sum -= hc & 0xff;
    sum -= address;
    sum -= cmd;
    sum -= ext;

    if ((sum < 6) || (sum > 0xC + 2)) {
        return DECODE_FAIL_SANITY;
    }

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
            "model",        "", DATA_COND,  (sum < 0xc),    DATA_STRING,    "FS20",
            "model",        "", DATA_COND, !(sum < 0xc),    DATA_STRING,    "FHT",
            "housecode",    "", DATA_FORMAT, "%x", DATA_INT, hc_b4,
            "address",      "", DATA_FORMAT, "%x", DATA_INT, ad_b4,
            "command",      "", DATA_STRING, (sum < 0xc) ? cmd_tab[cmd & 0x1f] : fht_cmd_tab[cmd & 0xf],
            "flags",        "", DATA_STRING, (sum < 0xc) ? flags_tab[cmd >> 5] : fht_flags_tab[cmd >> 5],
            "ext",          "", DATA_FORMAT, "%x", DATA_INT, ext,
            "mic",          "Integrity",    DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "housecode",
        "address",
        "command",
        "flags",
        "ext",
        NULL,
};

r_device const fs20 = {
        .name        = "FS20 / FHT",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 400,
        .long_width  = 600,
        .reset_limit = 9000,
        .decode_fn   = &fs20_decode,
        .fields      = output_fields,
};
