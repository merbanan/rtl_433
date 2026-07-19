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
    IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIPPPPPPPPPPPPPPPPXXXX

- PREAMBLE: 36-bits 0xF5555555E (or ...5557 with the 0b10 as the first MC bit)
- F: FLAGS, 3 Manchester encoded bits (4 bits with the fixed 1-bit from preamble)
- I: ID, 24 Manchester encoded bits
- P: PRESSURE, 8 Manchester encoded bits (PSI * 5)
- X: CHECK, 2 Manchester encoded bits, 2-bit addition checksum

Flags:

- 0 = learning mode (when using a 125kHz coil to wake up the TPMS and have the car learn; sends 9 packets)
- 3 = sudden pressure change (increase/decrease) sends 7 or 8 packets
- 5 = wake up mode, sends 8 packets
- 7 = driving mode, sends 8 packets

NOTE: there is NO temperature data transmitted

We use OOK_PULSE_PCM to get the bitstream above.
Then we use bitbuffer_manchester_decode() which will alert us to any
bit sequence which is not a valid Manchester transition. This enables a sanity
check on the Manchester pulses which is important for detecting possible
corruption since there is no CRC.

The Manchester bits are encoded as 01 => 0 and 10 => 1, which is
the reverse of bitbuffer_manchester_decode(), so we invert the result.

Checksum: pad the 36 data bits with the fixed 1-bit tail of the preamble to
get an even 38 bits, split into 2-bit groups, and add them together (not
XOR) modulo 4. The result is always 1. Found by RonNiles, see
https://github.com/merbanan/rtl_433/issues/1734

NOTE: this decoder cannot tell an SMD3MA4 (PSI * 5) apart from a
Schrader/Nissan/Infiniti MRXNIS315G3 sensor (also sold as the aftermarket
Redi-Sensor SE10001HP/SE10001HPR), which uses the same wire format and
preamble but only has 8 significant pressure bits at PSI * 4. These are
two separate decoders (schrader_SMD3MA4 and schrader_NIS315G3) rather than
one decoder emitting both interpretations, per
https://github.com/merbanan/rtl_433/issues/1734#issuecomment-4957207247 --
the model key should denote one protocol interpretation, not several. Both
are enabled by default, so a single physical transmission produces two
output records with two different pressure readings; use -R to select
just one if that duplication is undesirable.

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
#define NUM_BITS_DATA (38) // 1 fixed bit + 3 flags + 24 id + 8 pressure + 2 checksum
#define NUM_BITS_TOTAL_MIN (NUM_BITS_PREAMBLE / 2 + 2 * NUM_BITS_DATA)
#define NUM_BITS_TOTAL_MAX (NUM_BITS_PREAMBLE + 2 * NUM_BITS_DATA + 8)
#define SCHRADER_SMD3MA4 1
#define SCHRADER_NIS315G3 2

// Shared by schrader_SMD3MA4 and schrader_NIS315G3, which are otherwise
// wire-format identical and differ only in the pressure scale and model name.
static int schrader_SMD3MA4_family_decode(r_device *decoder, bitbuffer_t *bitbuffer, int model)
{
    // full preamble is 0xF5555555E, this is its last 16 bits
    uint8_t const preamble_pattern[2] = {0x55, 0x5e};

    // Reject wrong length, with margin of error for a short preamble or extra bits at the end
    if (bitbuffer->bits_per_row[0] < NUM_BITS_TOTAL_MIN
            || bitbuffer->bits_per_row[0] >= NUM_BITS_TOTAL_MAX) {
        return DECODE_ABORT_LENGTH;
    }

    // Find the preamble tail, keeping its last two (fixed) bits as the start of the data
    unsigned bitpos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, 16);
    bitpos += 14;
    if (bitpos + NUM_BITS_DATA * 2 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }

    // Check and decode the Manchester bits
    bitbuffer_t decoded = {0};
    unsigned ret = bitbuffer_manchester_decode(bitbuffer, 0, bitpos,
            &decoded, NUM_BITS_DATA);
    if (ret != bitpos + NUM_BITS_DATA * 2) {
        decoder_log(decoder, 2, __func__, "invalid Manchester data");
        return DECODE_FAIL_MIC;
    }
    bitbuffer_invert(&decoded);
    uint8_t *b = decoded.bb[0];

    // reject all-zero data
    if (!b[0] && !b[1] && !b[2] && !b[3]) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00");
        return DECODE_FAIL_SANITY;
    }

    // Checksum: add all 2-bit groups (including the fixed leading 1-bit) modulo 4, expect 1
    int sum = 0;
    for (int i = 0; i < 5; ++i) {
        sum += ((b[i] >> 0) & 0x3) + ((b[i] >> 2) & 0x3)
                + ((b[i] >> 4) & 0x3) + ((b[i] >> 6) & 0x3);
    }
    if ((sum & 0x3) != 1) {
        decoder_log(decoder, 2, __func__, "checksum failed");
        return DECODE_FAIL_MIC;
    }

    // Get the decoded data fields
    // 1FFFSSSS SSSSSSSS SSSSSSSS SSSSPPPP PPPPXX..
    int flags     = (b[0] & 0x70) >> 4;
    int serial_id = ((b[0] & 0x0f) << 20) | (b[1] << 12) | (b[2] << 4) | (b[3] >> 4);
    int pressure  = ((b[3] & 0x0f) <<  4) | (b[4] >> 4);

    // normal driving is flags == 0x7
    int flag_learn  = flags == 0x0;
    int flag_alarm  = flags == 0x3;
    int flag_wakeup = flags == 0x5;

    char id_str[9];
    snprintf(id_str, sizeof(id_str), "%06X", serial_id);

    float pressure_scale = 0.2f; // SCHRADER_SMD3MA4
    if (model != SCHRADER_SMD3MA4) {
        pressure_scale = 0.25f; // SCHRADER_NIS315G3
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_COND, model == SCHRADER_SMD3MA4, DATA_STRING, "Schrader-SMD3MA4",
            "model",            "",             DATA_COND, model == SCHRADER_NIS315G3, DATA_STRING, "Schrader-NIS315G3",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "ID",           DATA_STRING, id_str,
            "flags",            "Flags",        DATA_INT,    flags,
            "learn",            "Learn",        DATA_COND,   flag_learn, DATA_INT, 1,
            "alarm",            "Alarm",        DATA_COND,   flag_alarm, DATA_INT, 1,
            "wakeup",           "Wakeup",       DATA_COND,   flag_wakeup, DATA_INT, 1,
            "pressure_PSI",     "Pressure",     DATA_FORMAT, "%.1f PSI", DATA_DOUBLE, pressure * pressure_scale,
            "mic",              "Integrity",    DATA_STRING, "PARITY",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa schrader_SMD3MA4_family_decode() */
static int schrader_SMD3MA4_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    return schrader_SMD3MA4_family_decode(decoder, bitbuffer, SCHRADER_SMD3MA4);
}

/** @sa schrader_SMD3MA4_family_decode() */
static int schrader_NIS315G3_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    return schrader_SMD3MA4_family_decode(decoder, bitbuffer, SCHRADER_NIS315G3);
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
- P: pressure 9 bits, 1 kPa/bit
- C: 2 bits integrity check (C1, C2)
- T: 8 bits temperature offset by 50, range -50 to 205 degrees C

The leading wake+sync+start (W SSSSSSSSSSSSS s) is a fixed 16-bit
`0111111111111111` in every real capture seen so far (issue #3611: the
2-bit integrity check alone lets through ~1 in 4 noise bursts, verifying
this fixed prefix rejects far more of them).

Integrity check (C1C2):
The 2-bit integrity value is computed over the 35-bit payload (III+PPP+CC),
i.e. id, pressure, and the check bits themselves, but not the flags:
    C1C2 = (even_ones + 2*n - 1) mod 4
where:
    even_ones = count of 1-bits at even positions (0, 2, 4, ...) in the 35-bit payload
    n = total number of 1-bits in the 35-bit payload

Sample data (35-bit payload + C1C2), validated with an RDC test tool:

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

    if (bitbuffer->num_rows != 1)
        return DECODE_ABORT_EARLY;

    /* Check for incorrect number of bits received */
    if (bitbuffer->bits_per_row[0] != 61)
        return DECODE_ABORT_LENGTH;

    /* Verify the fixed 16-bit wake+sync+start prefix instead of assuming it */
    uint8_t const sync_pattern[] = {0x7f, 0xff};
    if (bitbuffer_search(bitbuffer, 0, 0, sync_pattern, 16) != 0) {
        decoder_log(decoder, 2, __func__, "sync pattern not found");
        return DECODE_ABORT_EARLY;
    }

    /* Discard the first 16 bits (wake + sync + start) */
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

    flags       = (b[0] >> 5) & 0x7;
    pressure    = ((b[3] & 0x1f) << 4) | (b[4] >> 4);
    temperature = ((b[4] & 0x03) << 5) | (b[5] >> 3);

    /* The 2-bit integrity check above only rejects 3 out of 4 random/corrupt
       payloads. Add a plausibility bound on pressure and temperature as a
       second, independent filter for the noise the parity check lets
       through. Temperature is bounded to a real tire's operating range
       (well under the sensor chip's own -40/+125 C survival rating -- a
       tire never actually reaches that in normal use) rather than the
       full field range. */
    if (pressure > 450 || temperature - 50 < -40 || temperature - 50 > 85) {
        decoder_logf(decoder, 2, __func__,
                "implausible pressure/temperature: %u kPa, %d C", pressure, temperature - 50);
        return DECODE_FAIL_SANITY;
    }

    snprintf(id_str, sizeof(id_str), "%06X", serial_id);
    snprintf(flags_str, sizeof(flags_str), "%01x", flags);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Schrader-MRXBC5A4",
            "type",             "",             DATA_STRING, "TPMS",
            "flags",            "",             DATA_STRING, flags_str,
            "id",               "ID",           DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, pressure * 1.0f,
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
        "learn",
        "alarm",
        "wakeup",
        "pressure_PSI",
        "mic",
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
        .name        = "Schrader TPMS SMD3MA4 (Subaru)",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 120,
        .long_width  = 120,
        .reset_limit = 480,
        .decode_fn   = &schrader_SMD3MA4_decode,
        .fields      = output_fields_SMD3MA4,
};

r_device const schrader_NIS315G3 = {
        .name        = "Schrader TPMS MRXNIS315G3, 3039 (Infiniti, Nissan, Renault), aka Redi-Sensor SE10001HP/SE10001HPR",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 120,
        .long_width  = 120,
        .reset_limit = 480,
        .decode_fn   = &schrader_NIS315G3_decode,
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
