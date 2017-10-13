/* X10 sensor
 *
 *
 * Stub for decoding test data only
 *
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "rtl_433.h"

static int X10_RF_callback(bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;

	uint8_t arrbKnownConstBitMask[4]  = {0x0B, 0x0B, 0x87, 0x87};
	uint8_t arrbKnownConstBitValue[4] = {0x00, 0x0B, 0x00, 0x87};
	uint8_t bKnownConstFlag = 1;

	// Row [0] is sync pulse
	// Validate package
	if ((bitbuffer->bits_per_row[1] == 32)		// Dont waste time on a short package
	// && (bb[1][0] == (uint8_t)(~bb[1][1]))		// Check integrity - apparently some chips may use both bytes..
	 && (bb[1][2] == ((0xff & (~bb[1][3]))))		// Check integrity
	)
	{
		fprintf(stdout, "X10 RF:\n");
		fprintf(stdout, "data    = %02X %02X %02X %02X", bb[1][0], bb[1][1], bb[1][2], bb[1][3]);

		// For the CR12A X10 Remote, with the exception of the SCAN buttons, some bits are constant.
		for (int8_t bIdx = 0; bIdx < 4; bIdx++)
		{
			uint8_t bTest = arrbKnownConstBitMask[bIdx] & bb[1][bIdx];  // Mask the appropriate bits

			if (bTest != arrbKnownConstBitValue[bIdx])  // If resulting bits are incorrectly set
			{
				bKnownConstFlag = 0;  // Set flag to 0, so decoding doesn't occur
			}
		}

		if (bKnownConstFlag == 1)  // If constant bits are appropriately set
		{
			uint8_t bHouseCode  = 0;
			uint8_t bDeviceCode = 0;
			uint8_t arrbHouseBits[4] = {0, 0, 0, 0};

			// Extract House bits
			arrbHouseBits[0] = (bb[1][0] & 0x80) >> 7;
			arrbHouseBits[1] = (bb[1][0] & 0x40) >> 6;
			arrbHouseBits[2] = (bb[1][0] & 0x20) >> 5;
			arrbHouseBits[3] = (bb[1][0] & 0x10) >> 4;

			// Convert bits into integer
			bHouseCode   = (~(arrbHouseBits[0] ^ arrbHouseBits[1])  & 0x01) << 3;
			bHouseCode  |= ( ~arrbHouseBits[1]                      & 0x01) << 2;
			bHouseCode  |= ( (arrbHouseBits[1] ^ arrbHouseBits[2])  & 0x01) << 1;
			bHouseCode  |=    arrbHouseBits[3]                      & 0x01;

			// Extract and convert Unit bits to integer
			bDeviceCode  = (bb[1][0] & 0x04) << 1;
			bDeviceCode |= (bb[1][2] & 0x40) >> 4;
			bDeviceCode |= (bb[1][2] & 0x08) >> 2;
			bDeviceCode |= (bb[1][2] & 0x10) >> 4;

			fprintf(stdout, "\t%c:%d", bHouseCode + 'A', bDeviceCode + 1);

			if ((bb[1][2] & 0x20) == 0x00)
			{
				fprintf(stdout, " ON");
			}
			else
			{
				fprintf(stdout, " OFF");
			}
		}

		fprintf(stdout, "\n");
		return 1;
	}
	return 0;
}


r_device X10_RF = {
	.name			= "X10 RF",
	.modulation		= OOK_PULSE_PPM_RAW,
	.short_limit	= 1100,	// Short gap 500µs, long gap 1680µs
	.long_limit		= 2800,	// Gap after sync is 4.5ms (1125)
	.reset_limit	= 6000, // Gap seen between messages is ~40ms so let's get them individually
	.json_callback	= &X10_RF_callback,
	.disabled		= 1,
	.demod_arg		= 0,
};
