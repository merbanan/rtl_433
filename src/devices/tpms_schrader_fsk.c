/** @file
    Schrader FSK TPMS sensor (Hyundai i20).

    Copyright (C) 2026 Dheeraj Reddy (acentauri92)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Schrader FSK TPMS sensor (Hyundai i20).

FSK Manchester variant from Schrader Electronics (Sensata). Distinct from the OOK
Schrader variants in schraeder.c and from the FSK Schrader Motorcycle variant.
Seen on a Hyundai i20 (2021). One decoder covers both factory part numbers.

    52940-BV100  model AFFPA4  FCC ID MRXAFFPA4  (IC 2546A-AFFPA4)
    52940-E2100  model BG6FD4  FCC ID MRXBG6FD4  (IC 2546A-BG6FD4)

RF Signal:
    FSK, Manchester coded, half-bit 52.18 us (s = l = 52).
    Manchester polarity is the inverse of IEEE 802.3: 1 = high-low, 0 = low-high.

A transmission starts with an idle preamble (0101...), ending in a "..0110"
transition, then the payload. The decoder syncs on the last idle byte plus the
transition (0x55 0x56) rather than a fixed offset. The sensor transmits in bursts
of 4 frames while the vehicle is rolling.

Flex decoder:

    rtl_433 -X 'n=Schrader-FSK,m=FSK_PCM,s=52,l=52,r=150'

    Decoded frames (9 bytes / 72 bits, after Manchester decode):

    Rolling (byte7=0x1c): 9187740a 54 07 58 1c 65
    Rolling (byte7=0x1c): 9187740a 54 17 58 1c 75  (byte5 burst counter steps)
    Pressure-loss (0x01): 9187740a 4e 00 5d 01 42

Data layout (72 bits):

    IIIIIIII IIIIIIII IIIIIIII IIIIIIII PPPPPPPP PURRUQQQ TTTTTTTT MMMMMMMM CCCCCCCC
    byte 0   byte 1   byte 2   byte 3   byte 4   byte 5   byte 6   byte 7   byte 8

- I: {32} Sensor ID (bytes 0-3), hexadecimal string in output.
- P: {9} Raw tire pressure = (byte4 << 1) | (byte5 >> 7) [8 bits in byte4 + MSB of byte5].
    Pressure in PSI = raw * 0.254 + 0.3, converted to kPa for output.
- R: {2} Burst repeat counter (byte5 bits 5-4)
- Q: {3} Sequence (byte5 bits 2-0)
- U: {2} Unknown (byte5 bits 6 and 3)
- T: {8} Temperature, degrees C = byte6 - 55.
- M: {8} Transmit Mode / Status (byte7). PARTIAL enumeration:
    - 0x1c: Rolling (normal; most common)
    - 0x1d: Rolling (hard accel/brake, tentative)
    - 0x0e: Rolling (less common, tentative)
    - 0x01: Pressure-loss alert
    Other states not yet observed.
- C: {8} Checksum = sum(byte0..byte7) & 0xFF.

*/

#include "decoder.h"

static int tpms_schrader_fsk_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Sync on the final idle byte and transition immediately before the payload.
    uint8_t const preamble[] = {0x55, 0x56};
    int events               = 0;
    int ret                  = DECODE_ABORT_EARLY;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned len    = bitbuffer->bits_per_row[row];
        unsigned bitpos = 0;

        // A complete message needs the 16-bit sync followed by 144 Manchester bits.
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos, preamble, 16)) + 16 + 144 <= len) {

            unsigned payload_pos = bitpos + 16;

            bitbuffer_t decoded = {0};
            bitbuffer_manchester_decode(bitbuffer, row, payload_pos, &decoded, 72);

            if (decoded.bits_per_row[0] < 72) {
                decoder_logf(decoder, 2, __func__, "Manchester decode stopped after %u bits",
                        decoded.bits_per_row[0]);
                ret = DECODE_FAIL_SANITY;
                bitpos += 16;
                continue;
            }

            // This sensor uses the opposite polarity to rtl_433's Manchester decoder.
            bitbuffer_invert(&decoded);

            uint8_t *b       = decoded.bb[0];
            uint8_t checksum = add_bytes(b, 8) & 0xff;

            if (checksum != b[8]) {
                decoder_logf(decoder, 2, __func__,
                        "Checksum error: calculated %02x, received %02x",
                        checksum, b[8]);
                ret = DECODE_FAIL_MIC;
                bitpos += 16;
                continue;
            }

            uint32_t id = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
            // The 9-bit pressure count was calibrated against physical digital gauge readings.
            unsigned pressure_raw = ((unsigned)b[4] << 1) | (b[5] >> 7);
            double pressure_kpa   = (pressure_raw * 0.254 + 0.3) * 6.895;
            // Byte 5 also carries a 2-bit index within a burst and a 3-bit sequence counter.
            unsigned burst_repeat = (b[5] >> 4) & 0x03;
            unsigned sequence     = b[5] & 0x07;
            int temperature_c     = b[6] - 55;
            unsigned mode         = b[7];

            char id_str[9];
            snprintf(id_str, sizeof(id_str), "%08x", id);

            /* clang-format off */
            data_t *data = data_make(
                    "model",         "",             DATA_STRING, "Schrader-FSK",
                    "type",          "",             DATA_STRING, "TPMS",
                    "id",            "",             DATA_STRING, id_str,
                    "pressure_kPa",  "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, pressure_kpa,
                    "temperature_C", "Temperature",  DATA_FORMAT, "%.0f C",   DATA_DOUBLE, (double)temperature_c,
                    "burst_repeat",  "Burst repeat", DATA_INT,    burst_repeat,
                    "sequence",      "Sequence",     DATA_INT,    sequence,
                    "mode",          "Mode",         DATA_FORMAT, "%02x",     DATA_INT,    mode,
                    "mic",           "Integrity",    DATA_STRING, "CHECKSUM",
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            events++;
            // Resume after this sync so the same preamble cannot be found again.
            bitpos += 16;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_kPa",
        "temperature_C",
        "burst_repeat",
        "sequence",
        "mode",
        "mic",
        NULL,
};

r_device const tpms_schrader_fsk = {
        .name        = "Schrader FSK TPMS (Hyundai)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 150,
        .decode_fn   = &tpms_schrader_fsk_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
