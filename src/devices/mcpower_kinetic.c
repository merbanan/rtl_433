/** @file
    McPower Kinetic battery-less wall switch.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
McPower Kinetic battery-less, kinetic-energy-harvesting 2-gang wall switch.

Reverse engineered in issue #3102 (\@Sola85, \@ProfBoc75). No raw capture
was ever attached to the issue, only demodulated hex pasted directly in
the thread -- this decoder and its test fixture are built from that hex,
not verified against a real IQ recording.

FSK_PCM, 100 kbps (10 us/bit). A press keeps transmitting the same 48 bit
frame for as long as the harvested energy lasts, each repeat preceded by
a `0xaaaa` preamble:

    ID:16h ?1b BUTTON_LEFT:1b BUTTON_RIGHT:1b ?1b COUNTER:4d FLAGS:8h CRC:16h

- ID: 16 bit, only a single physical unit was ever tested, so it's
  unconfirmed whether this is a real per-device identifier
- BUTTON_LEFT/BUTTON_RIGHT: which side of the rocker was pressed; both 0
  when the frame comes from residual harvested energy with no button
  held -- confirmed against 3 separately labeled captures (left/right/none)
- COUNTER: 4 bit, increments every transmission, wraps after 16
- FLAGS: 8 bit, always 0x40 in every sample seen, meaning unknown
- CRC: CRC-16 poly 0x1021 init 0xaa55 over the preceding 4 bytes, cracked
  with reveng against 9 samples (\@ProfBoc75), independently reconfirmed
  here against the same 9 samples before writing this decoder
*/
static int mcpower_kinetic_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[2] = {0xaa, 0xaa};

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int pos = bitbuffer_search(bitbuffer, 0, 0, preamble, 16);
    if (pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }
    pos += 16;

    if (bitbuffer->bits_per_row[0] - pos < 48) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[6];
    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, 48);

    uint16_t crc      = crc16(b, 4, 0x1021, 0xaa55);
    uint16_t crc_recv = (b[4] << 8) | b[5];
    if (crc != crc_recv) {
        decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %04x", crc, crc_recv);
        return DECODE_FAIL_MIC;
    }

    int id           = (b[0] << 8) | b[1];
    int button_left  = (b[2] >> 6) & 1;
    int button_right = (b[2] >> 5) & 1;
    int counter      = b[2] & 0xf;
    int flags        = b[3];

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "McPower-Kinetic",
            "id",           "",             DATA_FORMAT, "%04x", DATA_INT, id,
            "button_left",  "Left button",  DATA_INT,    button_left,
            "button_right", "Right button", DATA_INT,    button_right,
            "counter",      "Counter",      DATA_INT,    counter,
            "flags",        "Flags",        DATA_FORMAT, "%02x", DATA_INT, flags,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button_left",
        "button_right",
        "counter",
        "flags",
        "mic",
        NULL,
};

r_device const mcpower_kinetic = {
        .name        = "McPower Kinetic battery-less wall switch",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 10,
        .long_width  = 10,
        .reset_limit = 300,
        .decode_fn   = &mcpower_kinetic_decode,
        .fields      = output_fields,
};
