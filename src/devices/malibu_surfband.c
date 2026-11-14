/** @file
    Malibu Boats Surf Band remote.

    Copyright (C) 2026 Luke Young

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Malibu Boats Surf Band remote, 8 buttons, model 26DRF8/LR2000.

FCC ID 2AFN315RM01. The transmitter is a SiLabs Si4031 using its packet
handler with default settings: preamble, sync word `0x2dd4`, a length byte,
the payload, and CRC-16 IBM. There is no whitening and no Manchester coding.

FSK_PCM, 1200 baud (833 us/bit) at 433.00 MHz (not 433.92 MHz), with a very
small frequency deviation: about 4.1 kHz between the two tones (+-2 kHz).
One unit measured 432.997 MHz with a calibrated receiver, within the
tolerance of its crystal.

Each transmission is about 123 ms long and repeats every 131 ms while a
button is held:

    PREAMBLE:40b SYNC:16h LEN:8h 'B' 'U' BTN:8h BTN:8h 'O' 'N' BTN:8h '\\r' CRC:16h

- LEN: always 0x08, the number of payload bytes that follow
- BTN: the button code, sent three times
- CRC: CRC-16 poly 0x8005 init 0x0000 over LEN and the payload

A brief press sends one frame with the button code, then a Release frame
(0x00). Holding a button repeats a Hold frame (0xaa) every 131 ms, followed
by a Release frame. There is no device ID, so all remotes look the same.

Button codes:

- 0x01: Surf Left (SW1)
- 0x07: Surf Right (SW3)
- 0x0a: Volume Up (SW4)
- 0x03: Volume Down (SW9)
- 0x02: Wedge Up (SW5)
- 0x05: Wedge Down (SW6)
- 0x08: Speed Up (SW7)
- 0x0b: Speed Down (SW8)
- 0xaa: Hold (repeat while held)
- 0x00: Release

Volume Up/Down follow the faceplate icons; the PCB silkscreen labels them
the other way around (SW4_VOL-, SW9_VOL+).

The deviation is too small for the classic and min/max FSK pulse detectors
alone, but the default (auto) detector decodes it. Samples are in
rtl_433_tests `tests/malibu/surfband`.
*/
static char const *malibu_surfband_button_name(int button)
{
    switch (button) {
    case 0x00: return "Release";
    case 0x01: return "Surf Left";
    case 0x02: return "Wedge Up";
    case 0x03: return "Volume Down";
    case 0x05: return "Wedge Down";
    case 0x07: return "Surf Right";
    case 0x08: return "Speed Up";
    case 0x0a: return "Volume Up";
    case 0x0b: return "Speed Down";
    case 0xaa: return "Hold";
    default: return "Unknown";
    }
}

static int malibu_surfband_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Si443x default sync word, the preamble ends in either polarity
    uint8_t const sync[2] = {0x2d, 0xd4};

    int ret = DECODE_ABORT_EARLY;
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned pos = bitbuffer_search(bitbuffer, row, 0, sync, 16);
        if (pos >= bitbuffer->bits_per_row[row]) {
            continue;
        }
        pos += 16;

        if (bitbuffer->bits_per_row[row] - pos < 88) {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }

        uint8_t b[11];
        bitbuffer_extract_bytes(bitbuffer, row, pos, b, 88);

        if (b[0] != 0x08) {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }

        if (crc16(b, 11, 0x8005, 0x0000) != 0) {
            decoder_log_bitrow(decoder, 1, __func__, b, 88, "CRC invalid");
            ret = DECODE_FAIL_MIC;
            continue;
        }

        if (b[1] != 'B' || b[2] != 'U' || b[5] != 'O' || b[6] != 'N' || b[8] != '\r'
                || b[3] != b[4] || b[3] != b[7]) {
            decoder_log_bitrow(decoder, 1, __func__, b, 88, "Unexpected payload");
            ret = DECODE_FAIL_SANITY;
            continue;
        }

        int button = b[3];

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "Malibu-SurfBand",
                "button",       "Button",       DATA_FORMAT, "%02x", DATA_INT, button,
                "button_name",  "Button name",  DATA_STRING, malibu_surfband_button_name(button),
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return ret;
}

static char const *const output_fields[] = {
        "model",
        "button",
        "button_name",
        "mic",
        NULL,
};

r_device const malibu_surfband = {
        .name        = "Malibu Boats Surf Band remote",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 833,
        .long_width  = 833,
        .reset_limit = 20000,
        .decode_fn   = &malibu_surfband_decode,
        .fields      = output_fields,
};
