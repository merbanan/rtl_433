/** @file
    Airpuxem TYH11_EU6_ZQ FSK 84 bits Manchester encoded TPMS data.

    Copyright (C) 2019 Alexander Grau, Bruno Octau, Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Airpuxem TPMS Model TYH11_EU6_ZQ FSK.
- Working Temperature:-40 °C to 125 °C
- Working Frequency: 433.92MHz+-30KHz
- Tire monitoring range value: 100kPa-900kPa+-7kPa
- Based on SENASIC SNP739D TPMS IC ( https://www.senasic.com/Public/Uploads/uploadfile2/files/20240206/DS0069SNP739D0XDatasheet.pdf )
- Probably a 'white-labeled' Jansite TPMS ( http://www.jansite.cn/P_view.asp?pid=232 )

Data layout (nibbles):

    F  II II II II   M N  PP  TT  BB  CC  CC

- F: 4 bit Sync (5)
- I: 32 bit ID
- M: 1 bit Pressure MSB_ONE, 3 bit Flags
- N: 1 bit Pressure MSB TWO, 3 bit Sensor position
- P: 8 bit Pressure LSB (kPa)
- T: 8 bit Temperature (deg. C)
- B: 8 bit Battery level  (a good guess)
- C: 8 bit Checksum
- The preamble is 0xaa..aa9 (or 0x55..556 depending on polarity)
*/

#include "decoder.h"

static int tpms_airpuxem_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t dec = {0};
    uint8_t *b;

    // Decode up to ~200 bits of Manchester into a temp buffer
    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &dec, 354);
    if (dec.bits_per_row[0] < 84) {  // need at least 4 ("FIVE") + 64 (CRC'ed data) + 8 (CRC) + 8 (CRC again) = 84 bits
        return DECODE_FAIL_SANITY;
    }

    b = dec.bb[0];

    unsigned nbits  = dec.bits_per_row[0];

    // Basic structural checks
    if ((b[0] >> 4) != 0x5) {
        return DECODE_FAIL_SANITY; // header nibble mismatch
    }

    // Compute CRC over 84 bits starting right after the 4-bit constant header (FIVE)
    uint8_t payload[16] = {0};
    bitbuffer_extract_bytes(&dec, 0, 4, payload, 64);
    uint8_t crc_calc = crc8(payload, 8, 0x2f, 0xaa);

    // Extract two CRC bytes following the 4+64-bit payload
    uint8_t crcs[2] = {0};
    bitbuffer_extract_bytes(&dec, 0, 4 + 64, crcs, 16);
    if (crcs[0] != crc_calc) {
        decoder_logf(decoder, 2, __func__, "CRC mismatch calc=%02x exp0=%02x exp1=%02x len=%u", crc_calc, crcs[0], crcs[1], nbits);
        return DECODE_FAIL_MIC;
    }

    // Extract bitstream starting at bit offset 4
    uint8_t id_bytes[10] = {0};
    bitbuffer_extract_bytes(&dec, 0, 4, id_bytes, 10 * 8);
    unsigned id = ((unsigned)id_bytes[0] << 24) | (id_bytes[1] << 16) | (id_bytes[2] << 8) | id_bytes[3];


    char id_str[8 + 1];
    snprintf(id_str, sizeof(id_str), "%08x", id);

    int flags       = (id_bytes[4] >> 4) & 0x07;
    int position    = id_bytes[4] & 0x07;
    int pressure    = (id_bytes[5] | (((id_bytes[4] >> 7) & 1) << 8) | (((id_bytes[4] >> 3) & 1) << 9)) - 100;
    int temperature  = (char) id_bytes[6];
    int battery      =  id_bytes[7];

    char code_str[11 * 2 + 1];
    bitrow_snprint(b, 11 * 8, code_str, sizeof(code_str));

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Airpuxem-TYH11EU6ZQ",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "position",         "",             DATA_INT, position,
            "flags",            "",             DATA_INT, flags,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure ,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature ,
            "battery_V",        "Battery",      DATA_FORMAT, "%.1f V", DATA_DOUBLE, (double)battery * 0.02,
            "code",             "",             DATA_STRING, code_str,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_airpuxem_decode() */
static int tpms_airpuxem_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is (hex)
    // 5555555555555555555555555555555555555555555556

    // invert, search preamble on each row, then decode after it
    uint8_t const preamble_pattern[3] = {0xaa, 0xaa, 0xa9}; // after invert

    int ret    = 0;
    int events = 0;

    bitbuffer_invert(bitbuffer);

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos, preamble_pattern, 24)) + 80 <=
                bitbuffer->bits_per_row[row]) {
                ret = tpms_airpuxem_decode(decoder, bitbuffer, row, bitpos + 24);
            if (ret > 0)
                events += ret;
            bitpos += 2;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "position",
        "flags",
        "pressure_kPa",
        "temperature_C",
        "battery_V",
        "code",
        "mic",
        NULL,
};

r_device const tpms_airpuxem = {
        .name        = "Airpuxem TPMS TYH11_EU6_ZQ",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 150,
        .decode_fn   = &tpms_airpuxem_callback,
        .fields      = output_fields,
};
