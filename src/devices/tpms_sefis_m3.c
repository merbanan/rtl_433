/** @file
    Sefis M3 / Careud / Sykik SRTP300 TPMS.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn static int tpms_sefis_m3_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Sefis M3 / Careud / Sykik SRTP300 TPMS, built around an Infineon SP400 (8051
based) transceiver.

Reverse engineered in issue #3342 (\@ivantichy, \@ProfBoc75, \@zuckschwerdt).
FSK_PCM at 52 us/bit, Manchester (IEEE 802.3) coded, with a long 0xaa
preamble and a 16 bit sync word `0x5a9d`, followed by a 9 byte payload:

    B0 B1 B2 B3 B4 B5 B6 CRC:16

- CRC: CRC-16 poly 0x1021 init 0x0000 over the preceding 7 bytes,
  independently confirmed against 24 real frames from issue #3342
- Pressure: bytes B4/B5 form a 15 bit code, an odd non-sequential 2 bit
  "page" prefix (\@ivantichy's data: raw page bits 7/4/5/2 map to
  pages 0/1/2/3) taken from B4's top 3 bits, combined with B4's low 5
  bits and all of B5:

      page = {7:0, 4:1, 5:2, 2:3}[B4 >> 5]
      code = page << 13 | (B4 & 0x1f) << 8 | B5
      pressure_kPa = (code - 0x0e00) / 102.4

  Independently verified against a real 24-point 260->10 kPa deflation
  series (issue #3342, \@ivantichy, originally measured in bar): 20/24
  exact to the nearest 10 kPa, 24/24 within +-10 kPa. The 4 near-misses
  are all off by exactly -10 kPa (a consistent bias, not noise),
  suggesting the 0x0e00/102.4 constants are close but not perfectly
  calibrated -- flagged as a known small accuracy gap, not a wrong field.
- Temperature: bytes B2/B5 combine similarly:

      temp_code = (B2 + B5) & 0xff
      temperature_C = 14 + (temp_code & 0x0f)

  Verified against the same 24-point series (all but one sample at a
  constant 24 C): 23/24 exact.
- B3/B6 are not decoded -- they are known to participate in additional
  application-level state (the real receiver rejects some CRC-valid
  artificial payloads that only vary B3/B6), but their meaning wasn't
  solved. No fixed per-sensor ID field has been identified either.
  Reported as opaque hex in "code" for future reverse engineering.
*/

#define SEFIS_M3_SYNC_BITS 32
#define SEFIS_M3_PAYLOAD_BYTES 9 // B0-B6 + CRC:16
#define SEFIS_M3_PAYLOAD_BITS (SEFIS_M3_PAYLOAD_BYTES * 8)
#define SEFIS_M3_FRAME_BITS (SEFIS_M3_SYNC_BITS + SEFIS_M3_PAYLOAD_BITS * 2)

// Raw (pre-Manchester) sync word, decodes to 0x5a9d once XORed with 0xff
// below. This is the only polarity seen in the real captures from issue
// #3342; there's no verified sample of the opposite FSK polarity to
// justify handling it.
static uint8_t const sefis_m3_sync[4] = {0x66, 0x99, 0x96, 0xa6};

static int tpms_sefis_m3_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    // Validate message and reject it as fast as possible: check for the sync word.
    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, sefis_m3_sync, SEFIS_M3_SYNC_BITS);
    if (pos == bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY; // no sync word detected
    }
    if (pos + SEFIS_M3_FRAME_BITS > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }
    pos += SEFIS_M3_SYNC_BITS;

    bitbuffer_t packet = {0};
    bitbuffer_manchester_decode(bitbuffer, 0, pos, &packet, SEFIS_M3_PAYLOAD_BITS);
    if (packet.bits_per_row[0] < SEFIS_M3_PAYLOAD_BITS) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t b[SEFIS_M3_PAYLOAD_BYTES];
    for (int i = 0; i < SEFIS_M3_PAYLOAD_BYTES; i++) {
        b[i] = packet.bb[0][i] ^ 0xff;
    }

    uint16_t crc      = crc16(b, 7, 0x1021, 0x0000);
    uint16_t crc_recv = (b[7] << 8) | b[8];
    if (crc != crc_recv) {
        decoder_logf(decoder, 1, __func__, "CRC invalid %04x != %04x", crc, crc_recv);
        return DECODE_FAIL_MIC;
    }

    int pressure_page = -1;
    switch (b[4] >> 5) {
    case 7: pressure_page = 0; break;
    case 4: pressure_page = 1; break;
    case 5: pressure_page = 2; break;
    case 2: pressure_page = 3; break;
    }
    int has_pressure   = pressure_page >= 0;
    float pressure_kpa = 0.0f;
    if (has_pressure) {
        int code     = (pressure_page << 13) | ((b[4] & 0x1f) << 8) | b[5];
        pressure_kpa = (code - 0x0e00) / 102.4f;
        if (pressure_kpa < 0.0f) {
            pressure_kpa = 0.0f;
        }
    }

    int temp_code     = (b[2] + b[5]) & 0xff;
    int temperature_c = 14 + (temp_code & 0x0f);

    char code_str[15];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x%02x",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6]);

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",             DATA_STRING, "Sefis-M3",
            "type",          "",             DATA_STRING, "TPMS",
            "pressure_kPa",  "Pressure",      DATA_COND, has_pressure, DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure_kpa,
            "temperature_C", "Temperature",   DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature_c,
            "code",          "Undecoded data", DATA_STRING, code_str,
            "mic",           "Integrity",     DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "pressure_kPa",
        "temperature_C",
        "code",
        "mic",
        NULL,
};

r_device const tpms_sefis_m3 = {
        .name        = "Sefis M3 / Careud / Sykik SRTP300 TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 5000,
        .decode_fn   = &tpms_sefis_m3_decode,
        .fields      = output_fields,
};
