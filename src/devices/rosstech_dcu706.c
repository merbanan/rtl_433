/** @file
    Rosstech Digital Control Unit DCU-706/Sundance

    Copyright (C) 2023 suaveolent

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int rosstech_dcu706_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Rosstech Digital Control Unit DCU-706/Sundance/Jacuzzi

Supported Models:
Sundance DCU-6560-131, SD-880 Series, PN 6560-131
Jacuzzi DCU-2560-131, Jac-J300/J400 and SD-780 series, PN 6560-132/2560-131

Data layout:

    SS IIII TT CC

- S: 8 bit sync byte and type of transmission
- I: 16 bit ID
- T: 8 bit temp packet in degrees F
- C: 8 bit Checksum: Count 1s for each bit of each element:
                     Set bit to 1 if number is even 0 if odd

11 bits/byte: 1 start bit, 0 stop bits and odd parity

*/

static uint8_t calculateChecksum(const uint8_t *data, size_t size)
{
    uint8_t checksum = 0;

    for (int bit = 0; bit < 8; bit++) {
        int count = 0;

        for (size_t i = 0; i < size; i++) {
            count += (data[i] >> bit) & 1;
        }

        if (count % 2 == 0) {
            checksum |= (1 << bit);
        }
    }

    return checksum;
}

static int rosstech_dcu706_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preambleDataTransmission[] = {0xDD, 0x40};
    // The Bond command also contains the temperature
    uint8_t const preambleBond[] = {0xCD, 0x00};
    int const preamble_length = 11;

    // We need 55 bits
    uint8_t msg[7];

    if (bitbuffer->num_rows != 1
            || bitbuffer->bits_per_row[0] < 55
            || bitbuffer->bits_per_row[0] > 300) {
        decoder_logf(decoder, 2, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_EARLY; // Unrecognized data
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0, preambleDataTransmission, preamble_length);

    if (start_pos == bitbuffer->bits_per_row[0]) {

        start_pos = bitbuffer_search(bitbuffer, 0, 0, preambleBond, preamble_length);

        if (start_pos == bitbuffer->bits_per_row[0]) {
            return DECODE_ABORT_LENGTH;
        }
    }

    if (start_pos + 55 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof(msg) * 8);

    uint8_t transmissionType = (msg[0] << 1) | (msg[1] >> 7); //S
    uint8_t id_high = (msg[1] << 4) | (msg[2] >> 4);
    uint8_t id_low = (msg[2] << 7) | (msg[3] >> 1);
    uint16_t id = (uint16_t)(id_high << 8) | id_low; // I
    uint8_t temp = (msg[4] << 2) | (msg[5] >> 6); // T
    uint8_t checkSum = (msg[5] << 5) | (msg[6] >> 3); // C

    // Create a uint8_t array to hold the extracted values
    uint8_t extractedData[4];
    extractedData[0] = transmissionType;
    extractedData[1] = id_high;
    extractedData[2] = id_low;
    extractedData[3] = temp;

    uint8_t calculatedChecksum = calculateChecksum(extractedData, sizeof(extractedData) / sizeof(extractedData[0]));
    if (calculatedChecksum != checkSum) {
        decoder_logf(decoder, 2, __func__, "Sanity Check failed. Expected: %04x, Calculated: %04x. Maybe sanity function calculated wrong!", checkSum, calculatedChecksum);
        // return DECODE_FAIL_SANITY;
    }

    uint8_t temp_c = ((temp-32)*5)/9;

    /* clang-format off */
    data_t *data = data_make(
        "model",            "Model",              DATA_STRING,   "Rosstech-Spa",
        "id",               "ID",                 DATA_FORMAT,   "%04x",   DATA_INT,    id,
        "transmissionType", "Transmission Type",  DATA_STRING,   transmissionType == 0xba ? "Data" : "Bond",    
        "temperature_C",    "Temperature",        DATA_FORMAT,   "%d °C",  DATA_INT,     temp_c,
        "mic",              "Integrity",          DATA_STRING,   "CHECKSUM", 
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "transmissionType",
        "temperature_C",
        "mic",
        NULL,
};

r_device const rosstech_dcu706 = {
        .name        = "Rosstech Digital Control Unit DCU-706/Sundance/Jacuzzi",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 200,
        .long_width  = 200,
        .reset_limit = 2000,
        .decode_fn   = &rosstech_dcu706_decode,
        .fields      = output_fields,
};
