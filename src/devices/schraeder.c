/** @file
    Schrader TPMS protocol.

    Copyright (C) 2016 Benjamin Larsson
    and 2017 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Schrader TPMS decoder.

FCC-Id: MRXGG4

Packet payload: 1 sync nibble and 8 bytes data, 17 nibbles:

    0 12 34 56 78 9A BC DE F0
    7 f6 70 3a 38 b2 00 49 49
    S PF FI II II II PP TT CC

- S: sync
- P: preamble (0xf)
- F: flags
- I: id (28 bit)
- P: pressure from 0 bar to 6.375 bar, resolution of 25 mbar/hectopascal per bit
- T: temperature from -50 C to 205 C (1 bit = 1 temperature count 1 C)
- C: CRC8 from nibble 1 to E
*/

#include "decoder.h"

static int schraeder_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[8];
    int serial_id;
    char id_str[9];
    int flags;
    char flags_str[3];
    int pressure;    // mbar/hectopascal
    int temperature; // deg C

    /* Reject wrong amount of bits */
    if (bitbuffer->bits_per_row[0] != 68)
        return DECODE_ABORT_LENGTH;

    /* Shift the buffer 4 bits to remove the sync bits */
    bitbuffer_extract_bytes(bitbuffer, 0, 4, b, 64);

    /* Calculate the crc */
    if (b[7] != crc8(b, 7, 0x07, 0xf0)) {
        return DECODE_FAIL_MIC;
    }

    /* Get data */
    serial_id   = (b[1] & 0x0F) << 24 | b[2] << 16 | b[3] << 8 | b[4];
    flags       = (b[0] & 0x0F) << 4 | b[1] >> 4;
    pressure    = b[5] * 25;
    temperature = b[6] - 50;
    sprintf(id_str, "%07X", serial_id);
    sprintf(flags_str, "%02x", flags);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Schrader",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_STRING, flags_str,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, pressure * 0.1f,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
TPMS Model: Schrader Electronics EG53MA4.
Contributed by: Leonardo Hamada (hkazu).

Also Schrader PA66-GF35 (OPEL OEM 13348393) TPMS Sensor.

Probable packet payload:

    SSSSSSSSSS ???????? IIIIII TT PP CC

- S: sync
- ?: might contain the preamble, status and battery flags
- I: id (24 bits), could extend into flag bits (?)
- P: pressure, 25 mbar per bit
- T: temperature, degrees Fahrenheit
- C: checksum, sum of byte data modulo 256
*/
static int schrader_EG53MA4_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[10];
    int serial_id;
    char id_str[9];
    unsigned flags;
    char flags_str[9];
    int pressure;    // mbar
    int temperature; // degree Fahrenheit
    int checksum;

    /* Check for incorrect number of bits received */
    if (bitbuffer->bits_per_row[0] != 120)
        return DECODE_ABORT_LENGTH;

    /* Discard the first 40 bits */
    bitbuffer_extract_bytes(bitbuffer, 0, 40, b, 80);

    // No need to decode/extract values for simple test
    // check serial flags pressure temperature value not zero
    if ( !b[1] && !b[2] && !b[4] && !b[5] && !b[7] && !b[8] ) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: DECODE_FAIL_SANITY data all 0x00\n", __func__);
        }
        return DECODE_FAIL_SANITY;
    }

    /* Calculate the checksum */
    checksum = add_bytes(b, 9) & 0xff;
    if (checksum != b[9]) {
        return DECODE_FAIL_MIC;
    }

    /* Get data */
    serial_id   = (b[4] << 16) | (b[5] << 8) | b[6];
    flags       = ((unsigned)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    pressure    = b[7] * 25;
    temperature = b[8];
    sprintf(id_str, "%06X", serial_id);
    sprintf(flags_str, "%08x", flags);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Schrader-EG53MA4",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_STRING, flags_str,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, pressure * 0.1f,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, (double)temperature,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
SMD3MA4 Schrader TPMS used in Subaru.
Contributed by: RonNiles.

Refer to https://github.com/JoeSc/Subaru-TPMS-Spoofing

Data layout:

    ^^^^_^_^_^_^_^_^_^_^_^_^_^_^_^_^^^^_FFFFFFIIIIIIIIIIIII
    IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIPPPPPPPPPPPPPPPPXXXX

- PREAMBLE: 36-bits 0xF5555555E (or ...5557 with the 0b10 as the first MC bit)
- F: FLAGS, 3 Manchester encoded bits (4 bits with the fixed 1-bit from preamble)
- I: ID, 24 Manchester encoded bits
- P: PRESSURE, 8 Manchester encoded bits (PSI * 5)
- X: PRESSURE, 2 Manchester encoded bits checksum (2-bit addition)

Flags:

- 0 = learning mode (when using a 125kHz coil to wake up the TPMS and have the car learn; sends 9 packets)
- 3 = sudden pressure change (increase/decrease) sends 7 or 8 packets
- 5 = wake up mode, sends 8 packets
- 7 = driving mode, sends 8 packets

NOTE: there is NO temperature data transmitted

We use OOK_PULSE_PCM_RZ with .short_pulse = .long_pulse to get the bitstream
above. Then we use bitbuffer_manchester_decode() which will alert us to any
bit sequence which is not a valid Manchester transition. This enables a sanity
check on the Manchester pulses which is important for detecting possible
corruption since there is no CRC.

The Manchester bits are encoded as 01 => 0 and 10 => 1, which is
the reverse of bitbuffer_manchester_decode(), so we invert the result.
*/
#define NUM_BITS_PREAMBLE (36)
#define NUM_BITS_DATA (38)
#define NUM_BITS_TOTAL_MIN (NUM_BITS_PREAMBLE / 2 + 2 * NUM_BITS_DATA)
#define NUM_BITS_TOTAL_MAX (NUM_BITS_PREAMBLE + 2 * NUM_BITS_DATA + 8)

static int schrader_SMD3MA4_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is 0xF5555555E
    uint8_t const preamble_pattern[2] = {0x55, 0x5e}; // 16 bits

    // Reject wrong length, with margin of error for short preamble or extra bits at the end
    if (bitbuffer->bits_per_row[0] < NUM_BITS_TOTAL_MIN
            || bitbuffer->bits_per_row[0] >= NUM_BITS_TOTAL_MAX) {
        return DECODE_ABORT_LENGTH;
    }

    // Find a preamble with enough bits after it that it could be a complete packet
    unsigned bitpos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 16);
    bitpos += 14; // skip preamble but keep last two bits
    if (bitpos + NUM_BITS_DATA * 2 >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }

    // Check and decode the Manchester bits
    bitbuffer_t decoded = {0};

    unsigned ret = bitbuffer_manchester_decode(bitbuffer, 0, bitpos,
            &decoded, NUM_BITS_DATA);
    if (ret != bitpos + NUM_BITS_DATA * 2) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: invalid Manchester data\n", __func__);
        }
        return DECODE_FAIL_MIC;
    }
    bitbuffer_invert(&decoded);
    uint8_t *b = decoded.bb[0];

    // Reject all-zero data
    if (!b[0] && !b[1] && !b[2] && !b[3]) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: DECODE_FAIL_SANITY data all 0x00\n", __func__);
        }
        return DECODE_FAIL_SANITY;
    }

    // Checksum, note that this adds the preamble 1-bit as 0x10 also
    int sum = 0;
    for (int i = 0; i < 5; ++i) {
        sum += ((b[i] & 0x03) >> 0)
                + ((b[i] & 0x0c) >> 2)
                + ((b[i] & 0x30) >> 4)
                + ((b[i] & 0xc0) >> 6);
    }
    if ((sum & 0x3) != 1) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: Checksum failed\n", __func__);
        }
        return DECODE_FAIL_MIC;
    }

    // Get the decoded data fields
    // 1FFFSSSS SSSSSSSS SSSSSSSS SSSSPPPP PPPP XX
    int flags     = ((b[0] & 0x70) >> 4);
    int serial_id = ((b[0] & 0x0f) << 20) | (b[1] << 12) | (b[2] << 4) | (b[3] >> 4);
    int pressure  = ((b[3] & 0x0f) <<  4) | (b[4] >> 4);

    // normal driving is flags == 0x7
    int flag_learn  = flags == 0x0;
    int flag_alarm  = flags == 0x3;
    int flag_wakeup = flags == 0x5;

    char id_str[9];
    sprintf(id_str, "%06X", serial_id);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Schrader-SMD3MA4",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "ID",           DATA_STRING, id_str,
            "flags",            "Flags",        DATA_INT,    flags,
            "alarm",            "Alarm",        DATA_COND,   flag_alarm, DATA_INT, 1,
            "wakeup",           "Wakeup",       DATA_COND,   flag_wakeup, DATA_INT, 1,
            "learn",            "Learn",        DATA_COND,   flag_learn, DATA_INT, 1,
            "pressure_PSI",     "Pressure",     DATA_FORMAT, "%.1f PSI", DATA_DOUBLE, pressure * 0.2f,
            "mic",              "Integrity",    DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_C",
        "mic",
        NULL,
};

static char *output_fields_EG53MA4[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_F",
        "mic",
        NULL,
};

static char *output_fields_SMD3MA4[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_PSI",
        "mic",
        NULL,
};

r_device schraeder = {
        .name        = "Schrader TPMS",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 120,
        .long_width  = 0,
        .reset_limit = 480,
        .decode_fn   = &schraeder_decode,
        .fields      = output_fields,
};

r_device schrader_EG53MA4 = {
        .name        = "Schrader TPMS EG53MA4, PA66GF35",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 123,
        .long_width  = 0,
        .reset_limit = 300,
        .decode_fn   = &schrader_EG53MA4_decode,
        .fields      = output_fields_EG53MA4,
};

r_device schrader_SMD3MA4 = {
        .name        = "Schrader TPMS SMD3MA4 (Subaru)",
        .modulation  = OOK_PULSE_PCM_RZ,
        .short_width = 120,
        .long_width  = 120,
        .reset_limit = 480,
        .decode_fn   = &schrader_SMD3MA4_decode,
        .fields      = output_fields_SMD3MA4,
};
