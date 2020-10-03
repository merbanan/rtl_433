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

static int schraeder_callback(r_device *decoder, bitbuffer_t *bitbuffer)
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

    data = data_make(
            "model",            "",             DATA_STRING, "Schrader",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_STRING, flags_str,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure*0.1,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);

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
static int schrader_EG53MA4_callback(r_device *decoder, bitbuffer_t *bitbuffer)
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

    data = data_make(
            "model",            "",             DATA_STRING, "Schrader-EG53MA4",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_STRING, flags_str,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure*0.1,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, (double)temperature,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

/**
SMD3MA4 Schrader TPMS used in Subaru
Contributed by: RonNiles

Refer to https://github.com/JoeSc/Subaru-TPMS-Spoofing

^^^^_^_^_^_^_^_^_^_^_^_^_^_^_^_^^^^_FFFFFFIIIIIIIIIIIII
IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIPPPPPPPPPPPPPPPPPPPP

- PREAMBLE: 36-bits 0xF5555555E
- F: FLAGS, 3 Manchester encoded bits
- I: ID, 24 Manchester encoded bits
- P: PRESSURE, 10 Manchester encoded bits (PSI * 20)

NOTE: there is NO CRC and NO temperature data transmitted

We use OOK_PULSE_PCM_RZ with .short_pulse = .long_pulse to get the bitstream
above. Then we use bitbuffer_manchester_decode() which will alert us to any
bit sequence which is not a valid Manchester transition. This enables a sanity
check on the Manchester pulses which is important for detecting possible
corruption since there is no CRC.

The Manchester bits are encoded as 01 => 0 and 10 => 1, which is
the reverse of bitbuffer_manchester_decode(), so we invert the result.
*/
#define NUM_BITS_PREAMBLE (36)
#define NUM_BITS_FLAGS (3)
#define NUM_BITS_ID (24)
#define NUM_BITS_PRESSURE (10)
#define NUM_BITS_DATA (NUM_BITS_FLAGS + NUM_BITS_ID + NUM_BITS_PRESSURE)
#define NUM_BITS_TOTAL (NUM_BITS_PREAMBLE + 2 * NUM_BITS_DATA)

/**
utility function to get up to 32 bits from the bitbuffer as an
integer, while updating the offset for sequential calls
*/
static unsigned get_next_bits(bitbuffer_t *bitbuffer, unsigned *offset,
                              unsigned num_bits)
{
    uint8_t b;
    unsigned bits_now, result = 0;

    while (num_bits != 0) {
        bits_now = MIN(num_bits, 8);
        bitbuffer_extract_bytes(bitbuffer, 0, *offset, &b, bits_now);

        /* shift bits from MSB to LSB */
        if (bits_now < 8)
            b >>= (8 - bits_now);
        result = (result << bits_now) | b;

        num_bits -= bits_now;
        (*offset) += bits_now;
    }
    return result;
}

static int schrader_SMD3MA4_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t expected_preamble[] = { 0xF5, 0x55, 0x55, 0x55, 0xE0 };
    uint8_t received_preamble[sizeof(expected_preamble)];
    bitbuffer_t decoded = { 0 };
    char id_str[9];
    char flags_str[9];
    unsigned offset = 0, flags, serial_id, pressure;
    int ret;

    /* Reject wrong length, with margin of error for extra bits at the end */
    if (bitbuffer->bits_per_row[0] < NUM_BITS_TOTAL
            || bitbuffer->bits_per_row[0] >= NUM_BITS_TOTAL + 8) {
        return DECODE_ABORT_LENGTH;
    }

    /* Check preamble */
    bitbuffer_extract_bytes(bitbuffer, 0, 0, received_preamble, NUM_BITS_PREAMBLE);
    if (memcmp(received_preamble, expected_preamble, sizeof(expected_preamble)))
        return DECODE_FAIL_SANITY;

    /* Check and decode the Manchester bits */
    ret = bitbuffer_manchester_decode(bitbuffer, 0, NUM_BITS_PREAMBLE,
                                      &decoded, NUM_BITS_DATA);
    if (ret != NUM_BITS_TOTAL) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: invalid Manchester data\n", __func__);
        }
        return DECODE_FAIL_MIC;
    }
    bitbuffer_invert(&decoded);

    /* Get the decoded data fields */
    flags     = get_next_bits(&decoded, &offset, NUM_BITS_FLAGS);
    serial_id = get_next_bits(&decoded, &offset, NUM_BITS_ID);
    pressure  = get_next_bits(&decoded, &offset, NUM_BITS_PRESSURE);

    /* reject all-zero data */
    if (!flags && !serial_id && !pressure) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s: DECODE_FAIL_SANITY data all 0x00\n", __func__);
        }
        return DECODE_FAIL_SANITY;
    }

    sprintf(id_str, "%06X", serial_id);
    sprintf(flags_str, "%02x", flags);

    data = data_make(
            "model",            "",             DATA_STRING, "Schrader-SMD3MA4",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_STRING, flags_str,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_PSI",     "Pressure",     DATA_FORMAT, "%.2f PSI", DATA_DOUBLE, (double)pressure * 0.05,
            NULL);

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
        NULL,
};

r_device schraeder = {
        .name        = "Schrader TPMS",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 120,
        .long_width  = 0,
        .reset_limit = 480,
        .decode_fn   = &schraeder_callback,
        .disabled    = 0,
        .fields      = output_fields,
};

r_device schrader_EG53MA4 = {
        .name        = "Schrader TPMS EG53MA4, PA66GF35",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 123,
        .long_width  = 0,
        .reset_limit = 300,
        .decode_fn   = &schrader_EG53MA4_callback,
        .disabled    = 0,
        .fields      = output_fields_EG53MA4,
};

r_device schrader_SMD3MA4 = {
        .name        = "Schrader TPMS SMD3MA4",
        .modulation  = OOK_PULSE_PCM_RZ,
        .short_width = 120,
        .long_width  = 120,
        .reset_limit = 480,
        .decode_fn   = &schrader_SMD3MA4_callback,
        .disabled    = 0,
        .fields      = output_fields_SMD3MA4,
};
