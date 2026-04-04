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
    uint8_t b[8];
    int serial_id;
    int flags;
    int pressure;    // mbar/hectopascal
    int temperature; // deg C

    // Reject wrong amount of bits
    if (bitbuffer->bits_per_row[0] != 68)
        return DECODE_ABORT_LENGTH;

    // Shift the buffer 4 bits to remove the sync bits
    bitbuffer_extract_bytes(bitbuffer, 0, 4, b, 64);

    // Calculate the crc
    if (b[7] != crc8(b, 7, 0x07, 0xf0)) {
        return DECODE_FAIL_MIC;
    }

    // Get data
    serial_id   = (b[1] & 0x0F) << 24 | b[2] << 16 | b[3] << 8 | b[4];
    flags       = (b[0] & 0x0F) << 4 | b[1] >> 4;
    pressure    = b[5] * 25;
    temperature = b[6] - 50;

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%07X", serial_id);
    char flags_str[3];
    snprintf(flags_str, sizeof(flags_str), "%02x", flags);

    /* clang-format off */
    data_t *data = data_make(
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

Also Schrader Opel OEM No. 13348393 TPMS Sensor (might be found in Saab, Opel, Vauxhall, Chevrolet).
GM (Chevrolet) OEM No. 13540600 for 2006-2025 GM.

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

    // Check for incorrect number of bits received
    if (bitbuffer->bits_per_row[0] != 120)
        return DECODE_ABORT_LENGTH;

    // Discard the first 40 bits
    bitbuffer_extract_bytes(bitbuffer, 0, 40, b, 80);

    // No need to decode/extract values for simple test
    // check serial flags pressure temperature value not zero
    if (!b[1] && !b[2] && !b[4] && !b[5] && !b[7] && !b[8]) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00");
        return DECODE_FAIL_SANITY;
    }

    // Calculate the checksum
    checksum = add_bytes(b, 9) & 0xff;
    if (checksum != b[9]) {
        return DECODE_FAIL_MIC;
    }

    // Get data
    serial_id   = (b[4] << 16) | (b[5] << 8) | b[6];
    flags       = ((unsigned)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    pressure    = b[7] * 25;
    temperature = b[8];
    snprintf(id_str, sizeof(id_str), "%06X", serial_id);
    snprintf(flags_str, sizeof(flags_str), "%08x", flags);

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

Also Schrader 3039 TPMS for Infiniti, Nissan, Renault.
Contributed by: MotorvateDIY.

Refer to https://github.com/JoeSc/Subaru-TPMS-Spoofing

SCHRADER 3039 TPMS for Infiniti Nissan Renault (407001AY0A) (40700JY00B ?)
- https://catalogue.schradertpms.com/de-DE/ProductDetails/3039.html
- https://catalogue.schradertpms.com/en-GB/ProductDetails/3039.html
- Art.-Nr. 3039
- OE Art.-Nr: 407001AY0A
- EAN-Code: 5054208000275
- INFINITI, NISSAN, RENAULT (407001AY0A)

Used with:
- Nissan 370Z Z34 until 06/2014
- Infiniti FX until 12/2013
- Infiniti EX P53B (from 2007-10 until 2016-03)
- Infiniti FX (LCV) P53C (from 2008-03 until 2014-08)
- Infiniti FX P53C (from 2008-03 until 2014-08)
- Infiniti G L53A (from 2006-08 until 2013-03)
- Renault Koleos H45 (from 2008-02 until 2013-12)

Data layout:

    ^^^^_^_^_^_^_^_^_^_^_^_^_^_^_^_^^^^_FFFFFFIIIIIIIIIIIII
    IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIPPPPPPPPPPPPPPPPCCCC

- PREAMBLE: 36-bits 0xF5555555E
- F: FLAGS, 3 Manchester encoded bits
- I: ID, 24 Manchester encoded bits
- P: PRESSURE, 8 Manchester encoded bits (PSI * 5)
- C: CHECK, 2 Manchester encoded bits some kind of Parity

NOTE: there is NO temperature data transmitted
TODO: the checksum is unknown

We use OOK_PULSE_PCM to get the bitstream above.
Then we use bitbuffer_manchester_decode() which will alert us to any
bit sequence which is not a valid Manchester transition. This enables a sanity
check on the Manchester pulses which is important for detecting possible
corruption since there is no CRC.

The Manchester bits are encoded as 01 => 0 and 10 => 1, which is
the reverse of bitbuffer_manchester_decode(), so we invert the result.

Example payloads:

    {37}0000000030 {37}1000000020 {37}0800000028 {37}0400000020 {37}0200000028
    {37}0100000020 {37}0080000028 {37}0040000020 {37}0020000028 {37}0010000020
    {37}0008000028 {37}0004000020 {37}0002000028 {37}1400000030 {37}0a00000020
    {37}698e08eb48 {37}698e08ec68 {37}698e08ee60 {37}698e08edf0 {37}098e08edb8
    {37}098e08eca8 {37}098e08eb88 {37}098e08eb78 {37}098e08eb40 {37}098e08eb28
    {37}098e08eae0 {37}098e08eac8 {37}098e08eab0 {37}098e08ea98 {37}098e08ea68
    {37}098e08e8d0 {37}098e08e8b8 {37}098e08e880 {37}098e08e660 {37}098e08e3f8
    {37}698e08e2a0 {37}698e08e1e8 {37}098e08e028 {37}099b56e028 {37}099798e038

*/
#define NUM_BITS_PREAMBLE (36)
#define NUM_BITS_FLAGS (3)
#define NUM_BITS_ID (24)
#define NUM_BITS_PRESSURE (10)
#define NUM_BITS_DATA (NUM_BITS_FLAGS + NUM_BITS_ID + NUM_BITS_PRESSURE)
#define NUM_BITS_TOTAL (NUM_BITS_PREAMBLE + 2 * NUM_BITS_DATA)

static int schrader_SMD3MA4_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Reject wrong length, with margin of error for extra bits at the end
    if (bitbuffer->bits_per_row[0] < NUM_BITS_TOTAL
            || bitbuffer->bits_per_row[0] >= NUM_BITS_TOTAL + 8) {
        return DECODE_ABORT_LENGTH;
    }

    // Check preamble
    uint8_t *b = bitbuffer->bb[0];
    if (b[0] != 0xf5 || b[1] != 0x55 || b[2] != 0x55 || b[3] != 0x55
            || (b[4] >> 4) != 0xe) {
        return DECODE_FAIL_SANITY;
    }

    // Check and decode the Manchester bits
    bitbuffer_t decoded = {0};
    int ret = bitbuffer_manchester_decode(bitbuffer, 0, NUM_BITS_PREAMBLE,
            &decoded, NUM_BITS_DATA);
    if (ret != NUM_BITS_TOTAL) {
        decoder_log(decoder, 2, __func__, "invalid Manchester data");
        return DECODE_FAIL_MIC;
    }
    bitbuffer_invert(&decoded);
    b = decoded.bb[0];

    // Compute parity
    int parity = xor_bytes(b, 4) ^ (b[4] & 0xe0);
    parity     = (parity >> 4) ^ (parity & 0x0f);
    parity     = (parity >> 2) ^ (parity & 0x03);

    // Get the decoded data fields
    // FFFSSSSS SSSSSSSS SSSSSSSS SSSPPPPP PPPCCxxx
    int flags     = b[0] >> 5;
    int serial_id = ((b[0] & 0x1f) << 19) | (b[1] << 11) | (b[2] << 3) | (b[3] >> 5);
    int pressure  = ((b[3] & 0x1f) <<  3) | (b[4] >> 5);
    int check     = ((b[4] & 0x18) >> 3);

    decoder_logf_bitbuffer(decoder, 3, __func__, &decoded, "Parity: %d%d Check: %d%d", parity >> 1, parity & 1, check >> 1, check & 1);

    // reject all-zero data
    if (!flags && !serial_id && !pressure) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00");
        return DECODE_FAIL_SANITY;
    }

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%06X", serial_id);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Schrader-SMD3MA4",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_INT,    flags,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_PSI",     "Pressure",     DATA_FORMAT, "%.1f PSI", DATA_DOUBLE, pressure * 0.2f,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
TPMS Model: Schrader Electronics MRXBC5A4 / MRXBMW433TX1 (BMW)
Contributed by: Ilias Daradimos.

Packet structure (61 bits):

    W SSSSSSSSSSSSS s FFF IIIIIIIIIIIIIIIIIIIIIIII PPPPPPPPP CC TTTTTTTT

- W: 1 bit wake
- S: 13 sync bits
- s: 1 start bit
- F: 3 bits, may contain status and battery flags. Value 010 = sleep ACK.
- I: id (24 bits)
- P: pressure 9 bits, 2 kPa/bit
- C: 2 bits integrity check (C1, C2)
- T: 8 bits temperature offset by 50, range -50 to 205 degrees C

Integrity check (C1C2):
The 2-bit integrity value is computed over the 33-bit payload (FFF...PPP):
    C1C2 = (even_ones + 2*n - 1) mod 4
where:
    even_ones = count of 1-bits at even positions (0, 2, 4, ...) in the 33-bit payload
    n = total number of 1-bits in the 33-bit payload

Test data validated with RDC test tool (33-bit payload + C1C2):

    Single-bit patterns (shifting 1):
    00000000000000000000000000000000001 10
    00000000000000000000000000000000010 01
    00000000000000000000000000000000100 10
    00000000000000000000000000000001000 01
    00000000000000000000000000000010000 10
    00000000000000000000000000000100000 01
    00000000000000000000000000001000000 10
    00000000000000000000000000010000000 01
    00000000000000000000000000100000000 10
    00000000000000000000000001000000000 01
    00000000000000000000000010000000000 10
    00000000000000000000000100000000000 01
    00000000000000000000001000000000000 10
    00000000000000000000010000000000000 01
    00000000000000000000100000000000000 10
    00000000000000000001000000000000000 01
    00000000000000000010000000000000000 10
    00000000000000000100000000000000000 01
    00000000000000001000000000000000000 10
    00000000000000010000000000000000000 01
    00000000000000100000000000000000000 10
    00000000000001000000000000000000000 01
    00000000000010000000000000000000000 10
    00000000000100000000000000000000000 01
    00000000001000000000000000000000000 10
    00000000010000000000000000000000000 01
    00000000100000000000000000000000000 10
    00000001000000000000000000000000000 01
    00000010000000000000000000000000000 10
    00000100000000000000000000000000000 01
    00001000000000000000000000000000000 10
    00010000000000000000000000000000000 01
    00100000000000000000000000000000000 10

    Consecutive-bit patterns (shifting 5):
    00000000000000000000000000000011111 00
    00000000000000000000000000000111110 11
    00000000000000000000000000001111100 00
    00000000000000000000000000011111000 11
    00000000000000000000000000111110000 00
    00000000000000000000000001111100000 11
    00000000000000000000000011111000000 00
    00000000000000000000000111110000000 11
    00000000000000000000001111100000000 00

    Consecutive-bit patterns (shifting 9):
    0000000000000000000000000000111111111 10
    0000000000000000000000000001111111110 01

    Consecutive-bit patterns (shifting 13):
    00000000000000000000001111111111111 00
    00000000000000000000011111111111110 11
    00000000000000000000111111111111100 00

    Consecutive-bit patterns (shifting 17):
    00000000000000000011111111111111111 10
    00000000000000000111111111111111110 01

    Alternating patterns (shifting 101):
    00000000000000000000000000000000101 01
    00000000000000000000000000000001010 11
    00000000000000000000000000000010100 01
    00000000000000000000000000000101000 11

    Alternating patterns (shifting 10101):
    00000000000000000000000000000010101 00
    00000000000000000000000000000101010 01
    00000000000000000000000000001010100 00

    Sensor data (validated by RDC test tool):
    00000100010010000000001010000000001 11
    11100100010001101010010011001101010 10
    11100100010001101010010011000000000 01
    00000100010001101010010011000000000 01
    00100100010001101010010011000000000 00
    01000100010001101010010011000000000 11
    01100100010001101010010011000000000 10
    10000100010001101010010011000000000 00
    10100100010001101010010011000000000 11
    11000100010001101010010011000000000 10
*/
static int schrader_MRXBC5A4_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t b[6];
    int serial_id;
    char id_str[9];
    int flags;
    char flags_str[3];
    unsigned int pressure;    // kPa
    int temperature; // degree C

    /* Check for incorrect number of bits received */
    if (bitbuffer->bits_per_row[0] != 61)
        return DECODE_ABORT_LENGTH;

    /* Discard the first 15 bits (1 wake + 13 sync + 1 start) */
    bitbuffer_extract_bytes(bitbuffer, 0, 16, b, 46);

    /* Get data fields:
       b[0]: FFFIIIII (3 flags + 5 ID bits)
       b[1]: IIIIIIII (8 ID bits)
       b[2]: IIIIIIII (8 ID bits)
       b[3]: IIIPPPPP (3 ID + 5 pressure bits)
       b[4]: PPPPCC TT (4 pressure + 2 integrity + 2 temp)
       b[5]: TTTTTTxx (6 temp + 2 unused)
    */
    serial_id   = ((b[0] & 0x1f) << 19) | (b[1] << 11) | (b[2] << 3) | (b[3] >> 5);

    /* Check serial value not zero or all ones */
    if (serial_id == 0 || serial_id == 0xFFFFFF) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00");
        return DECODE_FAIL_SANITY;
    }

    flags       = (b[0] >> 5) & 0x7;
    pressure    = ((b[3] & 0x1f) << 4) | (b[4] >> 4);
    temperature = ((b[4] & 0x03) << 5) | (b[5] >> 3);

    /* Verify 2-bit integrity check (C1C2) over 35-bit payload (III+PPP+CC).
       C1C2 = (even_ones + 2*n - 1) mod 4
       where even_ones = count of 1-bits at even positions (0,2,4,...)
       and n = total number of 1-bits in the 35-bit payload.
       The 35-bit payload spans bits 3-37 of the extracted 46-bit data.
    */
    int even_ones = 0;
    int n = 0;
    for (int i = 3; i < 38; ++i) {
        int bit = (b[i / 8] >> (7 - (i % 8))) & 1;
        if (bit) {
            n++;
            if ((i - 3) % 2 == 0)
                even_ones++;
        }
    }
    int c1c2 = (even_ones + 2 * n - 1) & 0x3;
    int c1 = (b[4] >> 3) & 1;
    int c2 = (b[4] >> 2) & 1;
    if (c1c2 != ((c1 << 1) | c2)) {
        return DECODE_FAIL_MIC;
    }

    snprintf(id_str, sizeof(id_str), "%06X", serial_id);
    snprintf(flags_str, sizeof(flags_str), "%01x", flags);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Schrader-MRXBC5A4",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_STRING, flags_str,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, pressure * 2.0f,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temperature - 50,
            "sleep",            "Sleep",        DATA_STRING, (flags == 2 ? "True" : "False"),
            "mic",              "Integrity",    DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_C",
        "mic",
        NULL,
};

static char const *const output_fields_EG53MA4[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_F",
        "mic",
        NULL,
};

static char const *const output_fields_SMD3MA4[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_PSI",
        NULL,
};

static char const *const output_fields_MRXBC5A4[] = {
        "model",
        "type",
        "id",
        "flags",
        "sleep",
        "pressure_kPa",
        "temperature_C",
        "mic",
        NULL,
};

r_device const schraeder = {
        .name        = "Schrader TPMS",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 120,
        .long_width  = 0,
        .reset_limit = 480,
        .decode_fn   = &schraeder_decode,
        .fields      = output_fields,
};

r_device const schrader_EG53MA4 = {
        .name        = "Schrader TPMS EG53MA4, Saab, Opel, Vauxhall, Chevrolet",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 123,
        .long_width  = 0,
        .reset_limit = 300,
        .decode_fn   = &schrader_EG53MA4_decode,
        .fields      = output_fields_EG53MA4,
};

r_device const schrader_SMD3MA4 = {
        .name        = "Schrader TPMS SMD3MA4 (Subaru) 3039 (Infiniti, Nissan, Renault)",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 120,
        .long_width  = 120,
        .reset_limit = 480,
        .decode_fn   = &schrader_SMD3MA4_decode,
        .fields      = output_fields_SMD3MA4,
};

r_device const schrader_MRXBC5A4 = {
        .name        = "Schrader TPMS MRXBC5A4 (BMW)",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 123,
        .long_width  = 0,
        .reset_limit = 800,
        .decode_fn   = &schrader_MRXBC5A4_decode,
        .fields      = output_fields_MRXBC5A4,
};
