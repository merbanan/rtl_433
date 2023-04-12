/** @file
    Markisol (a.k.a E-Motion, BOFU, Rollerhouse, BF-30x, BF-415) curtains remote.

    Copyright (C) 2021 Dan Stahlke <dan@stahlke.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Markisol curtains remote.

Protocol description:
Each frame starts with:
    hi 4886us
    lo 2470us
    hi 1647us
    lo 315us
Then follow 40 bits:
    zero: hi 670us, lo 320us
    one : hi 348us, lo 642us

This is OOK_PULSE_PWM encoding.  The frame is erroneously interpred as a bit (so bitbuffer_t reports
41 bits rather than 40).  We discard this bit during recording.  The last frame erroneosly picks up
an extra bit at the end; we ignore this as well.

Packet interpretation:
    16 bits - unique ID of remote
    16 bits - channel, zone, and control
    8  bits - checksum (all bytes, including this one, sum to 1)

The second pack of 16 bits is interwoven:
    buf[2] & 0x0f - channel, in the range 1-15
    buf[2] & 0x20 - bit 0 of zone
    buf[2] & 0xd0 - bits 0,2,3 of control
    buf[3] & 0x10 - bit 1 of control
    buf[3] & 0x80 - bit 1 of zone
    buf[3] & 0x6f - unknown; for my remotes (buf[3] & 0x6f) == 0x01 always
*/

#include "decoder.h"

static int markisol_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t buf[5];
    uint8_t cksum = 0;
    int got_proper_row_length = 0;
    for (int i = 0; i < bitbuffer->num_rows; i++) {
        decoder_logf(decoder, 1, __func__, "bits_per_row[%d] = %d", i, bitbuffer->bits_per_row[i]);
        if (bitbuffer->bits_per_row[i] == 41 || bitbuffer->bits_per_row[i] == 42) {
            uint8_t *b = bitbuffer->bb[i];
            for (int j = 0; j < 5; ++j) {
                buf[j] = (b[j] << 1) + (b[j + 1] >> 7); // shift stream to discard spurious first bit
                buf[j] = ~reverse8(buf[j]);
                cksum += buf[j];
            }
            got_proper_row_length = 1;
            break;
        }
    }

    if (!got_proper_row_length)
        return DECODE_ABORT_EARLY;

    decoder_logf(decoder, 1, __func__, "%02x %02x %02x %02x %02x cksum=%02x", buf[0], buf[1], buf[2], buf[3], buf[4], cksum);

    if (cksum != 1)
        return DECODE_FAIL_MIC;

    int address = (buf[0] << 8) | buf[1];
    int channel = buf[2] & 0xf;
    int control = ((buf[2] >> 4) & ~2) | ((buf[3] & 0x10) >> 3);
    int zone    = ((buf[2] & 0x20) >> 5) + ((buf[3] & 0x80) >> 6) + 1;
    // buf[3] seems to be always 0x01, 0x11, 0x81, 0x91
    // ... so there are 6 bits that seem constant (for my remotes)

    char const *const control_strs[] = {
            "Limit (0)", // seems like Limit=0 for channel=1, otherwise Limit=13
            "Down (1)",
            "? (2)",
            "H-Down (3)",
            "Confirm (4)",
            "Stop (5)",
            "? (6)",
            "? (7)",
            "? (8)",
            "? (9)",
            "? (10)",
            "? (11)",
            "Up (12)",
            "Limit (13)",
            "H-Up (14)",
            "? (15)",
    };

    /* clang-format off */
    data_t *data = data_make(
            "model",          "Model",          DATA_STRING, "Markisol",
            "id",             "",               DATA_FORMAT, "%04X", DATA_INT, address,
            "control",        "Control",        DATA_STRING, control_strs[control],
            "channel",        "Channel",        DATA_INT,    channel,
            "zone",           "Zone",           DATA_INT,    zone,
            "mic",            "Integrity",      DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "control",
        "channel",
        "zone",
        "mic",
        NULL,
};

// rtl_433 -f 433900000 -X 'n=name,m=OOK_PWM,s=368,l=704,r=10000,g=10000,t=0,y=5628'

r_device const markisol = {
        .name        = "Markisol, E-Motion, BOFU, Rollerhouse, BF-30x, BF-415 curtain remote",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 368,
        .long_width  = 704,
        .sync_width  = 5628,
        .gap_limit   = 2000,
        .reset_limit = 2000,
        .decode_fn   = &markisol_decode,
        .fields      = output_fields,
};
