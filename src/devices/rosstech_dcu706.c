/** @file
    Rosstech Digital Control Unit DCU-706/Sundance

    Copyright (C) 2023 suaveolent

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Rosstech Digital Control Unit DCU-706/Sundance

...supported models etc.

Data layout:

    SS IIII TT CC

- S: 8 bit sync byte and type of transmission
- I: 16 bit ID
- T: 8 bit temp packet in degrees F
- C: 8 bit CRC-8, poly 0x81

11 bits/byte: 1 start bit, 0 stop bits and odd parity

*/




// Function to print an 8-bit integer as binary
static void printBinary(uint8_t value) {
    for (int i = 7; i >= 0; i--) {
        printf("%c", (value & (1 << i)) ? '1' : '0');
    }
}


static void printParity(char *chunk, int size) {

    int oneCount = 0;

    for(int i = 0; i<size; i++) {
        if(chunk[i] == '1') {
            oneCount++;
        }
    }

    printf(" %s (Count: %d)", oneCount % 2 == 0 ? "Even" : "Odd", oneCount);
    
}


static void printBinaryWithSpaces(const uint8_t *data, size_t length, size_t bitsPerChunk) {
    
    int chunkCount = 0;
    char currentChunk[bitsPerChunk];

    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        for (int j = 7; j >= 0; j--) {
            printf("%c", (byte & (1 << j)) ? '1' : '0');
            chunkCount++;
            currentChunk[chunkCount] = (byte & (1 << j)) ? '1' : '0';
            
            if (chunkCount % bitsPerChunk == 0) {
                printParity(currentChunk, bitsPerChunk);
                printf("\n");
                chunkCount = 0;
            }
        }
    }
    printf("\n");
}




static int rosstech_dcu706_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    uint8_t msg[7];


    if (bitbuffer->num_rows != 1
            || bitbuffer->bits_per_row[0] < 55
            || bitbuffer->bits_per_row[0] > 100) {
        decoder_logf(decoder, 2, __func__, "bit_per_row %u out of range", bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_EARLY; // Unrecognized data
    }

    uint8_t const preamble[] = {0xDD, 0x40};

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0, preamble, 11);

    if (start_pos == bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    if (start_pos + 55 > bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof(msg) * 8);

    for (size_t i = 0; i< sizeof(msg); i++) {
        printf("%04x ", msg[i]);
    }
    printf("\n");

    size_t msgLength = sizeof(msg) / sizeof(msg[0]);
    size_t bitsPerChunk = 11;

    printBinaryWithSpaces(msg, msgLength, bitsPerChunk);

    uint8_t syncType = (msg[0] << 1) | (msg[1] >> 7); //S
    uint16_t id = (uint16_t)(msg[1] << 4 | msg[2] >> 4) << 8 | msg[2] << 7 | msg[3] >> 1; // I
    uint8_t temp = msg[4] << 2 | msg[5] >> 6; // T
    uint8_t checkSum = msg[5] << 5 | msg[6] >> 3; // C


    printBinary(temp);

    int temp_int = temp;

    printf("Temp:  %du\n", temp_int);
    printf("Original uint8_t value: 0x%02X\n", temp);

    /* clang-format off */
    data_t *data = data_make(
        "model",            "Model",          DATA_STRING,   "Rosstech Digital Control Unit DCU-706/Sundance",
        "id",               "ID",             DATA_FORMAT,   "%04x",   DATA_INT,    id,
        "temperature_C",    "Temperature",    DATA_FORMAT,   "%d °F", DATA_INT, (int)temp,
        "mic",              "Integrity",      DATA_STRING,   "Check Sum",
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;

}

static char const *const output_fields[] = {
        "model",
        "id",
        "temp",
        "mic",
        NULL,
};

r_device const rosstech_dcu706 = {
        .name        = "Rosstech Digital Control Unit DCU-706/Sundance",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 200,
        .long_width  = 200,
        .sync_width  = 0, // 1:10, tuned to widely match 2450 to 2850
        .reset_limit = 2000,
        .decode_fn   = &rosstech_dcu706_decode,
        .fields      = output_fields,
};