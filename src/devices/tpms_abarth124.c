/**  @file
    VDO Type TG1C FSK 9 byte Manchester encoded checksummed TPMS data.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
(VDO Type TG1C via) Abarth 124 Spider TPMS decoded by TTigges
Protocol similar (and based on) Jansite Solar TPMS by Andreas Spiess and Christian W. Zuckschwerdt

OEM Sensor is said to be a VDO Type TG1C, available in different cars,
e.g.: Abarth 124 Spider, some Fiat 124 Spider, some Mazda MX-5 ND (and NC?) and probably some other Mazdas.
Mazda reference/part no.: BHB637140A
VDO reference/part no.: A2C1132410180

Compatible with aftermarket sensors, e.g. Aligator sens.it RS3

// Working Temperature: -50°C to 125°C
// Working Frequency: 433.92MHz+-38KHz
// Tire monitoring range value: 0kPa-350kPa+-7kPa (to be checked, VDO says 450/900kPa)

Data layout (nibbles):
    II II II II ?? PP TT SS CC
- I: 32 bit ID
- ?: 4 bit unknown (seems to change with status)
- ?: 4 bit unknown (seems static)
- P: 8 bit Pressure (multiplied by 1.38 = kPa)
- T: 8 bit Temperature (deg. C offset by 50)
- S: Status? (first nibble seems static, second nibble seems to change with status)
- C: 8 bit Checksum (Checksum8 XOR on bytes 0 to 8)
- The preamble is 0xaa..aa9 (or 0x55..556 depending on polarity)
*/

#include "decoder.h"

static int tpms_abarth124_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    int pressure;
    int temperature;
    int status;
    int checksum;

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 72);

    // make sure we decoded the expected number of bits
    if (packet_bits.bits_per_row[0] < 72) {
        // decoder_logf(decoder, 0, __func__, "bitpos=%u start_pos=%u = %u", bitpos, start_pos, (start_pos - bitpos));
        return 0; // DECODE_FAIL_SANITY;
    }

    b = packet_bits.bb[0];

    // check checksum (checksum8 xor)
    checksum = xor_bytes(b, 9);
    if (checksum != 0) {
        return 0; // DECODE_FAIL_MIC;
    }

    pressure    = b[5];
    temperature = b[6];
    status      = b[7];
    checksum    = b[8];

    char flags[1 * 2 + 1];
    snprintf(flags, sizeof(flags), "%02x", b[4]);
    char id_str[4 * 2 + 1];
    snprintf(id_str, sizeof(id_str), "%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Abarth-124Spider",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "flags",            "",             DATA_STRING, flags,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (float)pressure * 1.38,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (float)temperature - 50.0,
            "status",           "",             DATA_INT, status,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
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

    bitbuffer_invert(bitbuffer);
    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 24)) + 80 <=
            bitbuffer->bits_per_row[0]) {
        events += tpms_abarth124_decode(decoder, bitbuffer, 0, bitpos + 24);
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
        .name        = "Abarth 124 Spider TPMS",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 12-13 samples @250k
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_abarth124_callback,
        .fields      = output_fields,
};
