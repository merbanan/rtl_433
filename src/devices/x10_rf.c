/* X10 sensor
 *
 * Stub for decoding test data only
 *
 * Copyright (C) 2015 Tommy Vestermark
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "decoder.h"

static int x10_rf_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t *b = bitbuffer->bb[1];

    uint8_t arrbKnownConstBitMask[4]  = {0x0B, 0x0B, 0x87, 0x87};
    uint8_t arrbKnownConstBitValue[4] = {0x00, 0x0B, 0x00, 0x87};
    uint8_t bKnownConstFlag = 1;

    // Row [0] is sync pulse
    // Validate package
    if (bitbuffer->bits_per_row[1] != 32 // Don't waste time on a short package
            //|| (b[0] ^ b[1]) != 0xff // Check integrity - apparently some chips may use both bytes..
            || (b[2] ^ b[3]) != 0xff) // Check integrity
        return 0;

    unsigned code = (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];

    // For the CR12A X10 Remote, with the exception of the SCAN buttons, some bits are constant.
    for (int8_t bIdx = 0; bIdx < 4; bIdx++) {
        uint8_t bTest = arrbKnownConstBitMask[bIdx] & b[bIdx];  // Mask the appropriate bits

        if (bTest != arrbKnownConstBitValue[bIdx])  // If resulting bits are incorrectly set
            bKnownConstFlag = 0;  // Set flag to 0, so decoding doesn't occur
    }

    if (bKnownConstFlag != 1) // If constant bits are appropriately set
        return 0;

    uint8_t bHouseCode  = 0;
    uint8_t bDeviceCode = 0;
    uint8_t arrbHouseBits[4] = {0, 0, 0, 0};

    // Extract House bits
    arrbHouseBits[0] = (b[0] & 0x80) >> 7;
    arrbHouseBits[1] = (b[0] & 0x40) >> 6;
    arrbHouseBits[2] = (b[0] & 0x20) >> 5;
    arrbHouseBits[3] = (b[0] & 0x10) >> 4;

    // Convert bits into integer
    bHouseCode   = (~(arrbHouseBits[0] ^ arrbHouseBits[1])  & 0x01) << 3;
    bHouseCode  |= ( ~arrbHouseBits[1]                      & 0x01) << 2;
    bHouseCode  |= ( (arrbHouseBits[1] ^ arrbHouseBits[2])  & 0x01) << 1;
    bHouseCode  |=    arrbHouseBits[3]                      & 0x01;

    // Extract and convert Unit bits to integer
    bDeviceCode  = (b[0] & 0x04) << 1;
    bDeviceCode |= (b[2] & 0x40) >> 4;
    bDeviceCode |= (b[2] & 0x08) >> 2;
    bDeviceCode |= (b[2] & 0x10) >> 4;

    char housecode[2] = {0};
    *housecode = bHouseCode + 'A';

    int state = (b[2] & 0x20) == 0x00;

    data = data_make(
            "model",    "", DATA_STRING, "X10-RF",
            "houseid",  "", DATA_STRING, housecode,
            "deviceid", "", DATA_INT, bDeviceCode + 1,
            "state",    "", DATA_STRING, state ? "ON" : "OFF",
            "data",     "", DATA_FORMAT, "%08x", DATA_INT, code,
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "houseid"
    "deviceid",
    "state",
    "data",
    NULL
};

r_device X10_RF = {
    .name           = "X10 RF",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 500,  // Short gap 500µs
    .long_width     = 1680, // Long gap 1680µs
    .gap_limit      = 2800, // Gap after sync is 4.5ms (1125)
    .reset_limit    = 6000, // Gap seen between messages is ~40ms so let's get them individually
    .decode_fn      = &x10_rf_callback,
    .disabled       = 1,
    .fields         = output_fields,
};
