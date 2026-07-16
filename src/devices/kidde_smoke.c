/** @file
    Kidde RF-SM-DC wireless-interconnect smoke alarm (OOK, differential Manchester).

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Kidde RF-SM-DC wireless-interconnect smoke alarm (OOK, differential Manchester).

A 433/434 MHz OOK transmitter built into Kidde's wireless-interconnect smoke
and CO alarms, used so that one alarm going off triggers all paired units.
Protocol reverse engineered in https://github.com/merbanan/rtl_433/issues/2240:
the OOK envelope is differential Manchester coded (DMC) at a 400 us chip
width, identified there via https://triq.org/pdv/.

Decoded (post-DMC) message is 25 bits:

    0 IIIIIIII 01111111 JJJJJJJJ

- bit 0: fixed 0, framing/start bit.
- I: 8 bit "house code" (the alarm's 8 DIP switches), bit-reflected
  (transmitted LSB-first).
- middle byte: fixed 0x7f, used here as a search anchor (see below).
- J: the same 8 bit reflected house code again, XOR 0x80 (i.e. an exact
  second copy with just the top bit inverted) -- not a checksum (it does
  not depend on any bits outside the ID itself), but still used as a
  structural sanity check: reject unless J == (I ^ 0x80).

Reverse engineered from and verified byte-for-byte against 4 independent
real "heartbeat" messages captured from real, installed alarms (all 4 DIP
switch combinations 00000000/00001111/11111111/00111011 are covered),
posted to the issue as raw -F log dumps of `rtl_433 -R 0 -X
'n=name,m=OOK_PCM,s=400,l=400,g=1000,r=2000,decode_dm'` output. In each
capture the decoded 25 bit message above was, by a wide margin, the most
frequent exact bit pattern seen (hundreds of repeats vs. single digits for
any other pattern), which is what identified it as the real payload
(shorter/garbled variants come from the pulse slicer occasionally
resyncing mid-preamble).

Only this periodic "heartbeat"/presence message (sent whether or not the
alarm is going off) is confirmed here. Kidde's own documentation and the
GitHub issue both mention that alarm, test-button, and low-battery events
must be distinguishable somehow, but no real capture of any of those states
has ever been posted to the issue, so this decoder cannot report them --
every message it sees is treated as a plain presence/ID report.

The DIP-switch check above is weak (just an 8 bit echo of the ID, not a
real CRC over the whole message). A stronger noise guard by requiring
several exactly-matching repeated raw rows (the usual rtl_433 idiom for a
protocol without a strong checksum) was tried and does not work here: real
captures show repeats arriving 60-125 ms apart, well past rtl_433's pulse
detector's own (global, not device-configurable) ~10 ms cutoff for
considering two bursts part of the same reception -- every repeat always
arrives as its own separate single-row capture, so a same-capture repeated
row can never be found. The structural check above (fixed start bit, fixed
anchor byte, and the XOR-0x80 relationship) is the only validation
available; `.disabled = 1` reflects that.

Real captures always show one repeat per detected row (see above), so a
single row is expected. That row is differential-Manchester-decoded and
searched (via the fixed middle byte, used as an anchor) for the message.
The raw row starts with a non-DMC "de-sync" preamble of unknown length
(see the screenshot linked in the issue): a DMC decode from bit 0 reliably
aborts partway through it ("clock missing", i.e. two same-level raw bits
found where a transition was expected), so the decode is retried from
wherever the previous attempt gave up until one run decodes at least a
full message -- confirmed against real captures, where the preamble is
long enough that decoding from bit 0 alone never reaches the real payload.
*/

static int kidde_smoke_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = 0; // we expect a single row only
    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[row] < 25 * 2) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned row_len = bitbuffer->bits_per_row[row];

    // retry the DMC decode past the de-sync preamble, see file header.
    for (unsigned start = 0; start < row_len;) {
        bitbuffer_t decoded = {0};
        unsigned next = bitbuffer_differential_manchester_decode(bitbuffer, row, start, &decoded, 0);
        unsigned len  = decoded.bits_per_row[0];
        start         = next > start ? next : start + 1; // always make progress

        if (len < 25) {
            continue;
        }
        uint8_t const *b = decoded.bb[0];

        unsigned search_start = 9; // need 1 start bit + 8 id bits before the anchor
        while (search_start + 16 <= len) {
            unsigned pos = bitbuffer_search(&decoded, 0, search_start, (uint8_t[]){0x7f}, 8);
            if (pos + 16 > len) {
                break;
            }
            search_start = pos + 1;
            if (pos < 9) {
                continue;
            }

            // sanity: the fixed start bit right before the ID must be 0.
            if (bitrow_get_bit(b, pos - 9) != 0) {
                continue;
            }

            int id_refl = 0;
            for (int i = 0; i < 8; i++) {
                id_refl = (id_refl << 1) | bitrow_get_bit(b, pos - 8 + i);
            }
            int id2_refl = 0;
            for (int i = 0; i < 8; i++) {
                id2_refl = (id2_refl << 1) | bitrow_get_bit(b, pos + 8 + i);
            }
            if (id2_refl != (id_refl ^ 0x80)) {
                continue;
            }

            int id = reverse8((uint8_t)id_refl);

            /* clang-format off */
            data_t *data = data_make(
                    "model", "", DATA_STRING, "Kidde-Smoke",
                    "id",    "", DATA_FORMAT, "%02x", DATA_INT, id,
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            return 1;
        }
    }

    return DECODE_FAIL_SANITY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        NULL,
};

r_device const kidde_smoke = {
        .name        = "Kidde RF-SM-DC wireless-interconnect smoke alarm",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 400,
        .long_width  = 400,
        .reset_limit = 3000, // one message is ~20-26 ms of continuous chips, no internal gaps
        .decode_fn   = &kidde_smoke_decode,
        .fields      = output_fields,
        .disabled    = 1, // no real checksum, only a weak structural sanity check
};
