/** @file
    Hanwell ML/RL4000-series Radiologger temperature/humidity sensor.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Hanwell ML/RL4000-series Radiologger temperature/humidity sensor.

Likely an ML4106/RL4106 in its 12-bit radio mode. FSK PWM, centered at
434.052 MHz with tones roughly +21.9/+26.3 kHz either side -- midpoint
434.076 MHz matches the documented Hanwell 434.075 MHz channel (also
supports 433.920 MHz and synthesized 25 kHz-spaced channels).
Hanwell ML4106 datasheet: https://www.datenlogger-store.de/mwdownloads/download/link/id/788
ML4000 8/12-bit radio mode guide: https://www.catec.nl/uploads/pdf/Han-ml4000-guide_810.pdf

Preamble (24x short mark pairs) and sync (long mark pair, 3x the short
width) are handled by FSK_PULSE_PWM's own sync detection, no explicit
search needed. 40 data bits follow, each received byte bit-reversed
(least-significant bit transmitted first within a byte).

Data layout, after reverse8() on each byte:

    II HHHH TTTT hhhh|tttt CC

- I: 8 bit transmitter ID
- H: top 8 bits of a 12 bit humidity raw count
- T: top 8 bits of a 12 bit temperature raw count
- h/t: bottom 4 bits of humidity, then bottom 4 bits of temperature
- C: 8 bit additive checksum of the preceding 4 bytes

Engineering units are transmitter-specific (calibration lives in the
Hanwell base station), so this reports the two 12-bit raw counts, not a
guessed temperature/humidity conversion.

Reverse-engineered in issue #2942. Verified against 4 of 5 complete
40-bit frames in the issue's "15xID1" capture -- the other ~10 bursts in
that recording are truncated well short of 40 bits and don't decode.
*/

static int hanwell_ml4000_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_invert(bitbuffer);

    // the real 40-bit frame is always the last row; earlier rows are
    // leftover sync/preamble noise the demod couldn't fully lock out
    if (bitbuffer->num_rows < 1) {
        return DECODE_ABORT_EARLY;
    }
    int row = bitbuffer->num_rows - 1;
    if (bitbuffer->bits_per_row[row] != 40) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[5];
    bitbuffer_extract_bytes(bitbuffer, row, 0, b, 40);
    for (int i = 0; i < 5; ++i) {
        b[i] = reverse8(b[i]);
    }

    uint8_t checksum = (b[0] + b[1] + b[2] + b[3]) & 0xff;
    if (checksum != b[4]) {
        return DECODE_FAIL_MIC;
    }

    int id              = b[0];
    int humidity_raw    = (b[1] << 4) | (b[3] >> 4);
    int temperature_raw = (b[2] << 4) | (b[3] & 0x0f);

    /* clang-format off */
    data_t *data = data_make(
            "model",              "",                 DATA_STRING, "Hanwell-ML4000",
            "id",                 "",                 DATA_INT,    id,
            "temperature_raw",    "Temperature Raw",  DATA_INT,    temperature_raw,
            "humidity_raw",       "Humidity Raw",     DATA_INT,    humidity_raw,
            "mic",                "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "temperature_raw",
        "humidity_raw",
        "mic",
        NULL,
};

r_device const hanwell_ml4000 = {
        .name        = "Hanwell ML/RL4000-series Radiologger temperature/humidity sensor",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 1000,
        .long_width  = 2000,
        .sync_width  = 3000,
        .reset_limit = 10000,
        .decode_fn   = &hanwell_ml4000_decode,
        .fields      = output_fields,
        .disabled    = 1, // engineering-unit conversion unknown, needs field verification
};
