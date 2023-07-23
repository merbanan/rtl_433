/** @file
    VDO TPMS Type TG1C and Q85.
    Copyright (C) TTigges for (VDO Type TG1C via) Abarth 124 Spider TPMS decoded
    Protocol similar (and based on) Jansite Solar TPMS by Andreas Spiess and Christian W. Zuckschwerdt
    Copyright (C) 2023 Bruno OCTAU (ProfBoc75) Add Shenzhen EGQ Q85 support

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
VDO TPMS Type TG1C and Q85.
VDO Type TG1C                                  FSK  9 byte Manchester encoded, checksummed for 8 bytes TPMS data.
Q85 from Shenzhen EGQ Cloud technology CO,LTD, FSK 12 byte Manchester encoded, checksummed for 8 bytes TPMS data + CRC/16 CCITT-FASLE for 10 bytes DATA.

TG1C (Abarth-124Spider):
OEM Sensor is said to be a VDO Type TG1C, available in different cars,
e.g.: Abarth 124 Spider, some Fiat 124 Spider, some Mazda MX-5 ND (and NC?) and probably some other Mazdas.
Mazda reference/part no.: BHB637140A
VDO reference/part no.: A2C1132410180

Compatible with aftermarket sensors, e.g. Aligator sens.it RS3

// Working Temperature: -50°C to 125°C
// Working Frequency: 433.92MHz+-38KHz
// Tire monitoring range value: 0kPa-350kPa+-7kPa (to be checked, VDO says 450/900kPa)

Q85 (Shenzhen-EGQ-Q85):
Analysis asked by @js-3972 in issue #2556
TPMS from Amazon here: https://www.amazon.com/dp/B0BK8SHDRZ?psc=1&ref=ppx_yo2ov_dt_b_product_details
Air pressure setting range: 0.1~6.0BA / 1.45~87psi
Temperature setting range: 60°C~110°C / 140ºF~230ºF
Working temperature: -20°C~80°C / -68ºF~176ºF
Storage temperature: -30°C~85°C / 86ºF~185ºF
Power-on voltage: DC 5V
Frequency: 433.92MHz±20.00MHZ (remark: more probably ±20.00KHZ than MHZ)

Very similar to Abarth 124Spider, message lengh is bigger including a 0x40 then a CRC/16 CCITT-FALSE, (LSB first, then MSB)
Temperature (°C) offset is 55 C
Pressure (KPa) is divided by 3.

Both payload:

- The preamble is 0xaa..aa9 (or 0x55..556 depending on polarity)

Data layout (nibbles):

Byte    00 01 02 03 04 05 06 07 08 09 10 11
TG1C    II II II II ?? PP TT SS CC
Q85     II II II II ?? PP TT SS CC FF CR CR

- I: 32 bit ID
- ?: 4 bit unknown (seems to change with status)
- ?: 4 bit unknown (seems static)
- P: 8 bit Pressure (multiplyed by 1.38 = kPa for TG1C, by 3 for Q85)
- T: 8 bit Temperature (deg. C offset by 50 for TG1C, by 55 for Q85)
- S: Status? (first nibble seems static, second nibble seems to change with status)
- CC: 8 bit Checksum (Checksum8 XOR on bytes 0 to 8)
- F: 8 bit unknown (Q85 only, seems fixed = 0x40)
- CR: 16 bit CRC/16 CCITT-FASLE. little-Endian format (Q85 only, on bytes 0 to 9).
*/

#include "decoder.h"
#define MODEL_TG1C 1
#define MODEL_Q85  2

static int tpms_abarth124_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos, int type)
{
    data_t *data;
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    char id_str[4 * 2 + 1];
    char flags[1 * 2 + 1];
    int pressure, temperature, status, checksum, data_len;
    uint16_t crc_little_endian, crc_result;

    // 9 byte or 12 byte
    if (type == MODEL_TG1C) {
        data_len = 72;
    } else if (type == MODEL_Q85) {
        data_len = 96;
    } else {
        return 0; //return 0 instead of DECODE_ABORT_LENGTH, to avoid exit error : gave invalid return value -x: notify maintainer;
    }

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, data_len);

    // make sure we decoded the expected number of bits
    if (packet_bits.bits_per_row[0] < data_len) {
        // decoder_logf(decoder, 0, __func__, "bitpos=%u start_pos=%u = %u", bitpos, start_pos, (start_pos - bitpos));
        return 0; //DECODE_FAIL_SANITY;
    }

    b = packet_bits.bb[0];

    // check checksum (checksum8 xor) same for both type model
    checksum = xor_bytes(b, 9);
    if (checksum != 0) {
        return 0; //DECODE_FAIL_MIC;
    }

    if (type == MODEL_Q85) {
        // check CRC for 0 to 9 byte only if type Q85, CRC 16 CCITT-FALSE little-endian
        crc_little_endian = (b[11] << 8 ) | b[10];
        crc_result = crc16(b, 10, 0x1021,0xffff); // CRC-16 CCITT-FALSE
        if (crc_result != crc_little_endian) {
            return 0; //DECODE_FAIL_MIC;
        }
    }

    sprintf(flags, "%02x", b[4]);
    pressure    = b[5];
    temperature = b[6];
    status      = b[7];
    checksum    = b[8];
    sprintf(id_str, "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_COND, type == MODEL_TG1C, DATA_STRING, "Abarth-124Spider",
            "model",            "",             DATA_COND, type == MODEL_Q85,  DATA_STRING, "Shenzhen-EGQ-Q85",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "flags",            "",             DATA_STRING, flags,
            "pressure_kPa",     "Pressure",     DATA_COND, type == MODEL_TG1C, DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (float)pressure * 1.38,
            "pressure_kPa",     "Pressure",     DATA_COND, type == MODEL_Q85,  DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (float)pressure * 3,
            "temperature_C",    "Temperature",  DATA_COND, type == MODEL_TG1C, DATA_FORMAT, "%.0f C", DATA_DOUBLE, (float)temperature - 50.0,
            "temperature_C",    "Temperature",  DATA_COND, type == MODEL_Q85,  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (float)temperature - 55.0,
            "status",           "",             DATA_INT, status,
            "mic",              "Integrity",    DATA_COND, type == MODEL_TG1C, DATA_STRING, "CHECKSUM",
            "mic",              "Integrity",    DATA_COND, type == MODEL_Q85,  DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_abarth124_decode() */
static int tpms_abarth124_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // preamble
    uint8_t const preamble_pattern[3] = {0xaa, 0xaa, 0xa9}; // after invert

    unsigned bitpos = 0;
    int events      = 0;
    int type        = 0;

    bitbuffer_invert(bitbuffer);
    unsigned bits = bitbuffer->bits_per_row[0];

    // Define the type model , around 195 bits for TG1C MC encoded (result is 72 bits), around 353 bits for Q85 MC encoded (result is 96 bits)
    if (bits > 150 && bits < 210) {
        type = MODEL_TG1C;
    } else if ( bits > 210 && bits < 400) {
        type = MODEL_Q85;
    } else {
        return DECODE_ABORT_LENGTH;
    }

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 24)) + 80 <=
            bitbuffer->bits_per_row[0]) {
        events += tpms_abarth124_decode(decoder, bitbuffer, 0, bitpos + 24,type);
        bitpos += 2;
    }

    return events;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_C",
        "status",
        "code",
        "mic",
        NULL,
};

r_device const tpms_abarth124 = {
        .name        = "Abarth 124 Spider and Shenzhen EGQ Q85 TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 12-13 samples @250k
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_abarth124_callback,
        .fields      = output_fields,
};
