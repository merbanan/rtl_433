/** @file
    SmartFire Proflame 2 remote protocol.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>
    based on protocol decode Copyright (C) 2020 johnellinwood

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/** @fn int proflame2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
SmartFire Proflame 2 remote protocol.

See https://github.com/johnellinwood/smartfire

The command bursts are transmitted at 314,973 KHz using On-Off Keying (OOK).
Transmission rate is 2400 baud. Packet is transmitted 5 times, repetitions are separated by 12 low amplitude bits (zeros).

Encoded with a variant of Thomas Manchester encoding:
0 is represented by 01, a 1 by 10, zero padding (Z) by 00, and synchronization words (S) as 11.
Legacy packets contain 7 words (182 PCM bits); extended packets contain 10 words (260 PCM bits).
The last low half of the final end guard may be absent at the physical row end.

Each word contains 13 decoded symbols,
starts with a synchronization symbol, followed by a 1 as a guard bit,
then 8 bits of data, a padding bit, a parity bit, and finally a 1 as an end guard bit.
The padding bit is 1 for the first word and 0 for all other words.
The parity bit is calculated over the data bits and the padding bit,
and is 0 if there are an even number of ones and 1 if there are an odd number of ones.

The payload data is 7 bytes:

- Serial 1
- Serial 2
- Serial 3
- Command 1
- Command 2
- Error Detection 1
- Error Detection 2

Extended payloads contain serial W1-W3, commands W4/W5, raw W6-W8, and integrity W9/W10.
W9 = lane(W1, W3, W5, W7); W10 = lane(W2, W4, W6, W8).
For each lane start at zero and apply acc = T(acc XOR word), where T is the legacy
Err transform with C = D = 0. Both lanes must match; reported mic is DIGEST.
Legacy mic is PARITY (formerly CHECKSUM): learned command-integrity C/D is not checked.
W4 bit 7 is not interpreted as pilot in extended packets. Pilot is -1 (unknown)
until a capture-backed mapping permits 0/1 output. W6-W8 remain raw.
JSON id and individual bytes are integers; human-readable output formats them as hex.
raw_words is lowercase fixed-width hex in on-air order, without separators or prefix.

*/
#include "decoder.h"

typedef enum {
    PROFLAME2_LEGACY,
    PROFLAME2_EXTENDED,
} proflame2_variant_t;

typedef struct {
    uint8_t words[10];
    unsigned word_count;
    unsigned consumed_bits;
    proflame2_variant_t variant;
    int result;
} proflame2_frame_t;

// The PCM slicer retains up to two low bits after the last high pulse.
// One belongs to the final end guard; at most one further low bit is a gap.
static int proflame2_parse_words(bitbuffer_t const *bits, unsigned row,
        uint8_t *out, unsigned capacity, proflame2_frame_t *frame)
{
    unsigned length  = bits->bits_per_row[row];
    uint8_t const *b = bits->bb[row];
    unsigned pos     = 0;
    unsigned count   = 0;

    if (length > BITBUF_COLS * 8)
        return DECODE_ABORT_LENGTH;

    while (pos < length) {
        if (count && length - pos == 1 && !bitrow_get_bit(b, pos)) {
            ++pos;
            break;
        }
        if (count >= capacity || length - pos < 25)
            return DECODE_ABORT_LENGTH;

        // Sync and start guard must be 1110 for every word.
        if (!bitrow_get_bit(b, pos) || !bitrow_get_bit(b, pos + 1) || !bitrow_get_bit(b, pos + 2) || bitrow_get_bit(b, pos + 3))
            return DECODE_FAIL_SANITY;
        pos += 4;

        unsigned symbols = 0;
        for (unsigned symbol = 0; symbol < 11; ++symbol) {
            if (pos >= length)
                return DECODE_ABORT_LENGTH;
            unsigned high = bitrow_get_bit(b, pos++);
            if (pos == length) {
                // Only the final low half of the final end guard may be absent.
                if (symbol != 10 || !high || (count != 6 && count != 9))
                    return DECODE_ABORT_LENGTH;
            }
            else if (high == bitrow_get_bit(b, pos++)) {
                return DECODE_FAIL_SANITY;
            }
            symbols = (symbols << 1) | high;
        }

        uint8_t data = symbols >> 3;
        unsigned pad = (symbols >> 2) & 1;
        unsigned par = (symbols >> 1) & 1;
        if (pad != (count == 0) || !(symbols & 1))
            return DECODE_FAIL_SANITY;
        if (parity8(data) ^ pad ^ par)
            return DECODE_FAIL_MIC;

        out[count++] = data;
    }

    frame->word_count    = count;
    frame->consumed_bits = pos;
    return 1;
}

static int proflame2_classify(proflame2_frame_t *frame)
{
    if (frame->word_count == 7)
        frame->variant = PROFLAME2_LEGACY;
    else if (frame->word_count == 10)
        frame->variant = PROFLAME2_EXTENDED;
    else
        return DECODE_ABORT_LENGTH;
    return 1;
}

// Legacy Err transform with C = D = 0, in byte-width arithmetic.
static uint8_t proflame2_transform(uint8_t value)
{
    unsigned high = value >> 4;
    unsigned low  = value & 0x0f;
    return (((high ^ (high << 1) ^ (low << 1)) & 0x0f) << 4) | (high ^ low);
}

static int proflame2_integrity(proflame2_frame_t const *frame)
{
    if (frame->variant == PROFLAME2_LEGACY)
        return 1; // Word parity is checked; learned C/D values are unknown.

    uint8_t odd  = 0;
    uint8_t even = 0;
    for (unsigned i = 0; i < 8; i += 2) {
        odd  = proflame2_transform(odd ^ frame->words[i]);
        even = proflame2_transform(even ^ frame->words[i + 1]);
    }
    return odd == frame->words[8] && even == frame->words[9] ? 1 : DECODE_FAIL_MIC;
}

static void proflame2_output(r_device *decoder, proflame2_frame_t const *frame)
{
    uint8_t const *b = frame->words;
    int extended     = frame->variant == PROFLAME2_EXTENDED;
    int id           = b[0] << 16 | b[1] << 8 | b[2];
    char raw_words[21];
    for (unsigned i = 0; i < frame->word_count; ++i)
        snprintf(raw_words + 2 * i, sizeof(raw_words) - 2 * i, "%02x", b[i]);

    /* clang-format off */
    data_t *data = data_make(
            "protocol_variant", "Variant",  DATA_STRING, extended ? "extended_10_word" : "legacy_7_word",
            "model",        "",             DATA_STRING, "Proflame2-Remote",
            "id",           "Id",           DATA_FORMAT, "%06x", DATA_INT, id,
            "cmd1",         "Cmd1",         DATA_FORMAT, "%02x", DATA_INT, b[3],
            "cmd2",         "Cmd2",         DATA_FORMAT, "%02x", DATA_INT, b[4],
            "err1",         "Err1",         DATA_COND, !extended, DATA_FORMAT, "%02x", DATA_INT, b[5],
            "err2",         "Err2",         DATA_COND, !extended, DATA_FORMAT, "%02x", DATA_INT, b[6],
            "w6",           "W6",           DATA_COND, extended, DATA_FORMAT, "%02x", DATA_INT, b[5],
            "w7",           "W7",           DATA_COND, extended, DATA_FORMAT, "%02x", DATA_INT, b[6],
            "w8",           "W8",           DATA_COND, extended, DATA_FORMAT, "%02x", DATA_INT, b[7],
            "w9",           "W9",           DATA_COND, extended, DATA_FORMAT, "%02x", DATA_INT, b[8],
            "w10",          "W10",          DATA_COND, extended, DATA_FORMAT, "%02x", DATA_INT, b[9],
            "pilot",        "Pilot",        DATA_INT, extended ? -1 : b[3] >> 7,
            "light",        "Light",        DATA_INT, (b[3] & 0x70) >> 4,
            "thermostat",   "Thermostat",   DATA_INT, (b[3] & 0x02) >> 1,
            "power",        "Power",        DATA_INT, b[3] & 0x01,
            "front",        "Front",        DATA_INT, b[4] >> 7,
            "fan",          "Fan",          DATA_INT, (b[4] & 0x70) >> 4,
            "aux",          "Aux",          DATA_INT, (b[4] & 0x08) >> 3,
            "flame",        "Flame",        DATA_INT, b[4] & 0x07,
            "mic",          "Integrity",    DATA_STRING, extended ? "DIGEST" : "PARITY",
            "raw_words",    "Raw words",    DATA_STRING, raw_words,
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
}

static int proflame2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int result = DECODE_ABORT_LENGTH;
    for (unsigned row = 0; row < bitbuffer->num_rows; ++row) {
        proflame2_frame_t frame = {0};
        frame.result            = proflame2_parse_words(bitbuffer, row, frame.words, sizeof(frame.words), &frame);
        if (frame.result > 0)
            frame.result = proflame2_classify(&frame);
        if (frame.result > 0)
            frame.result = proflame2_integrity(&frame);
        if (frame.result <= 0) {
            decoder_logf(decoder, 2, __func__, "Rejected row %u: %d", row, frame.result);
            // Prefer a specific integrity/framing failure over an empty row.
            if (result == DECODE_ABORT_LENGTH || frame.result != DECODE_ABORT_LENGTH)
                result = frame.result;
            continue;
        }
        proflame2_output(decoder, &frame);
        return 1;
    }
    return result;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "protocol_variant",
        "cmd1",
        "cmd2",
        "err1",
        "err2",
        "w6",
        "w7",
        "w8",
        "w9",
        "w10",
        "raw_words",
        "pilot",
        "light",
        "thermostat",
        "power",
        "front",
        "fan",
        "aux",
        "flame",
        "mic",
        NULL,
};

r_device const proflame2 = {
        .name        = "SmartFire Proflame 2 remote control",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 417, // 2400 baud
        .long_width  = 417,
        .gap_limit   = 1000, // 12 low amplitudes are 5000 us
        .reset_limit = 6000,
        .decode_fn   = &proflame2_decode,
        .fields      = output_fields,
};
