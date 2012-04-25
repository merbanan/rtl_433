/*
 * Fitipower FC0013 tuner driver, taken from the kernel driver that can be found
 * on http://linux.terratec.de/tv_en.html
 *
 * This driver is a mess, and should be cleaned up/rewritten.
 *
 */

#include <stdint.h>

#include "rtlsdr_i2c.h"
#include "tuner_fc0013.h"

#define CRYSTAL_FREQ		28800000

/* glue functions to rtl-sdr code */
int FC0013_Write(void *pTuner, unsigned char RegAddr, unsigned char Byte)
{
	uint8_t data[2];

	data[0] = RegAddr;
	data[1] = Byte;

	if (rtlsdr_i2c_write_fn(pTuner, FC0013_I2C_ADDR, data, 2) < 0)
		return FC0013_I2C_ERROR;

	return FC0013_I2C_SUCCESS;
}

int FC0013_Read(void *pTuner, unsigned char RegAddr, unsigned char *pByte)
{
	uint8_t data = RegAddr;

	if (rtlsdr_i2c_write_fn(pTuner, FC0013_I2C_ADDR, &data, 1) < 0)
		return FC0013_I2C_ERROR;

	if (rtlsdr_i2c_read_fn(pTuner, FC0013_I2C_ADDR, &data, 1) < 0)
		return FC0013_I2C_ERROR;

	*pByte = data;

	return FC0013_I2C_SUCCESS;
}

int FC0013_SetVhfTrack(void *pTuner, unsigned long FrequencyKHz)
{
	unsigned char read_byte;

    if (FrequencyKHz <= 177500)	// VHF Track: 7
    {
		if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x1C) != FC0013_I2C_SUCCESS) goto error_status;

    }
    else if (FrequencyKHz <= 184500)	// VHF Track: 6
    {
		if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x18) != FC0013_I2C_SUCCESS) goto error_status;

    }
    else if (FrequencyKHz <= 191500)	// VHF Track: 5
    {
		if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x14) != FC0013_I2C_SUCCESS) goto error_status;

    }
    else if (FrequencyKHz <= 198500)	// VHF Track: 4
    {
		if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x10) != FC0013_I2C_SUCCESS) goto error_status;

    }
    else if (FrequencyKHz <= 205500)	// VHF Track: 3
    {
		if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x0C) != FC0013_I2C_SUCCESS) goto error_status;

    }
    else if (FrequencyKHz <= 212500)	// VHF Track: 2
    {
		if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x08) != FC0013_I2C_SUCCESS) goto error_status;

    }
    else if (FrequencyKHz <= 219500)	// VHF Track: 2
    {
		if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x08) != FC0013_I2C_SUCCESS) goto error_status;

    }
    else if (FrequencyKHz <= 226500)	// VHF Track: 1
    {
		if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x04) != FC0013_I2C_SUCCESS) goto error_status;
    }
    else	// VHF Track: 1
    {
		if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x04) != FC0013_I2C_SUCCESS) goto error_status;

    }

	//------------------------------------------------ arios modify 2010-12-24
	// " | 0x10" ==> " | 0x30"   (make sure reg[0x07] bit5 = 1)

	// Enable VHF filter.
	if(FC0013_Read(pTuner, 0x07, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x07, read_byte | 0x10) != FC0013_I2C_SUCCESS) goto error_status;

	// Disable UHF & GPS.
	if(FC0013_Read(pTuner, 0x14, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x14, read_byte & 0x1F) != FC0013_I2C_SUCCESS) goto error_status;


	return FC0013_FUNCTION_SUCCESS;

error_status:
	return FC0013_FUNCTION_ERROR;
}


// FC0013 Open Function, includes enable/reset pin control and registers initialization.
//void FC0013_Open()
int FC0013_Open(void *pTuner)
{
	// Enable FC0013 Power
	// (...)
	// FC0013 Enable = High
	// (...)
	// FC0013 Reset = High -> Low
	// (...)

	/* FIXME added to fix replug-bug with rtl-sdr */
	if(FC0013_Write(pTuner, 0x0C, 0x00) != FC0013_I2C_SUCCESS) goto error_status;

    //================================ update base on new FC0013 register bank
	if(FC0013_Write(pTuner, 0x01, 0x09) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x02, 0x16) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x03, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x04, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x05, 0x17) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x06, 0x02) != FC0013_I2C_SUCCESS) goto error_status;
//	if(FC0013_Write(pTuner, 0x07, 0x27) != FC0013_I2C_SUCCESS) goto error_status;		// 28.8MHz, GainShift: 15
	if(FC0013_Write(pTuner, 0x07, 0x2A) != FC0013_I2C_SUCCESS) goto error_status;		// 28.8MHz, modified by Realtek
	if(FC0013_Write(pTuner, 0x08, 0xFF) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x09, 0x6F) != FC0013_I2C_SUCCESS) goto error_status;		// Enable Loop Through
	if(FC0013_Write(pTuner, 0x0A, 0xB8) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x0B, 0x82) != FC0013_I2C_SUCCESS) goto error_status;

	if(FC0013_Write(pTuner, 0x0C, 0xFE) != FC0013_I2C_SUCCESS) goto error_status;   // Modified for up-dowm AGC by Realtek(for master, and for 2836BU dongle).
//	if(FC0013_Write(pTuner, 0x0C, 0xFC) != FC0013_I2C_SUCCESS) goto error_status;   // Modified for up-dowm AGC by Realtek(for slave, and for 2832 mini dongle).

//	if(FC0013_Write(pTuner, 0x0D, 0x09) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x0D, 0x01) != FC0013_I2C_SUCCESS) goto error_status;   // Modified for AGC non-forcing by Realtek.

	if(FC0013_Write(pTuner, 0x0E, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x0F, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x10, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x11, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x12, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x13, 0x00) != FC0013_I2C_SUCCESS) goto error_status;

	if(FC0013_Write(pTuner, 0x14, 0x50) != FC0013_I2C_SUCCESS) goto error_status;		// DVB-T, High Gain
//	if(FC0013_Write(pTuner, 0x14, 0x48) != FC0013_I2C_SUCCESS) goto error_status;		// DVB-T, Middle Gain
//	if(FC0013_Write(pTuner, 0x14, 0x40) != FC0013_I2C_SUCCESS) goto error_status;		// DVB-T, Low Gain

	if(FC0013_Write(pTuner, 0x15, 0x01) != FC0013_I2C_SUCCESS) goto error_status;


	return FC0013_FUNCTION_SUCCESS;

error_status:
	return FC0013_FUNCTION_ERROR;
}


int FC0013_SetFrequency(void *pTuner, unsigned long Frequency, unsigned short Bandwidth)
{
//    bool VCO1 = false;
//    unsigned int doubleVCO;
//    unsigned short xin, xdiv;
//	unsigned char reg[21], am, pm, multi;
    int VCO1 = FC0013_FALSE;
    unsigned long doubleVCO;
    unsigned short xin, xdiv;
	unsigned char reg[21], am, pm, multi;

	unsigned char read_byte;

	unsigned long CrystalFreqKhz;

	int CrystalFreqHz = rtlsdr_get_tuner_clock(pTuner);

	// Get tuner crystal frequency in KHz.
	// Note: CrystalFreqKhz = round(CrystalFreqHz / 1000)
	CrystalFreqKhz = (CrystalFreqHz + 500) / 1000;

	// modified 2011-02-09: for D-Book test
	// set VHF_Track = 7
	if(FC0013_Read(pTuner, 0x1D, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;

	// VHF Track: 7
    if(FC0013_Write(pTuner, 0x1D, (read_byte & 0xE3) | 0x1C) != FC0013_I2C_SUCCESS) goto error_status;


	if( Frequency < 300000 )
	{
		// Set VHF Track.
		if(FC0013_SetVhfTrack(pTuner, Frequency) != FC0013_I2C_SUCCESS) goto error_status;

		// Enable VHF filter.
		if(FC0013_Read(pTuner, 0x07, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x07, read_byte | 0x10) != FC0013_I2C_SUCCESS) goto error_status;

		// Disable UHF & disable GPS.
		if(FC0013_Read(pTuner, 0x14, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x14, read_byte & 0x1F) != FC0013_I2C_SUCCESS) goto error_status;
	}
	else if ( (Frequency >= 300000) && (Frequency <= 862000) )
	{
		// Disable VHF filter.
		if(FC0013_Read(pTuner, 0x07, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x07, read_byte & 0xEF) != FC0013_I2C_SUCCESS) goto error_status;

		// enable UHF & disable GPS.
		if(FC0013_Read(pTuner, 0x14, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x14, (read_byte & 0x1F) | 0x40) != FC0013_I2C_SUCCESS) goto error_status;
	}
	else if (Frequency > 862000)
	{
		// Disable VHF filter
		if(FC0013_Read(pTuner, 0x07, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x07, read_byte & 0xEF) != FC0013_I2C_SUCCESS) goto error_status;

		// Disable UHF & enable GPS
		if(FC0013_Read(pTuner, 0x14, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x14, (read_byte & 0x1F) | 0x20) != FC0013_I2C_SUCCESS) goto error_status;
	}

	if (Frequency * 96 < 3560000)
    {
        multi = 96;
        reg[5] = 0x82;
        reg[6] = 0x00;
    }
    else if (Frequency * 64 < 3560000)
    {
        multi = 64;
        reg[5] = 0x02;
        reg[6] = 0x02;
    }
    else if (Frequency * 48 < 3560000)
    {
        multi = 48;
        reg[5] = 0x42;
        reg[6] = 0x00;
    }
    else if (Frequency * 32 < 3560000)
    {
        multi = 32;
        reg[5] = 0x82;
        reg[6] = 0x02;
    }
    else if (Frequency * 24 < 3560000)
    {
        multi = 24;
        reg[5] = 0x22;
        reg[6] = 0x00;
    }
    else if (Frequency * 16 < 3560000)
    {
        multi = 16;
        reg[5] = 0x42;
        reg[6] = 0x02;
    }
    else if (Frequency * 12 < 3560000)
    {
        multi = 12;
        reg[5] = 0x12;
        reg[6] = 0x00;
    }
    else if (Frequency * 8 < 3560000)
    {
        multi = 8;
        reg[5] = 0x22;
        reg[6] = 0x02;
    }
    else if (Frequency * 6 < 3560000)
    {
        multi = 6;
        reg[5] = 0x0A;
        reg[6] = 0x00;
    }
    else if (Frequency * 4 < 3800000)
    {
        multi = 4;
        reg[5] = 0x12;
        reg[6] = 0x02;
    }
	else
	{
        Frequency = Frequency / 2;
		multi = 4;
        reg[5] = 0x0A;
        reg[6] = 0x02;
	}

    doubleVCO = Frequency * multi;

    reg[6] = reg[6] | 0x08;
//	VCO1 = true;
	VCO1 = FC0013_TRUE;

	// Calculate VCO parameters: ap & pm & xin.
//	xdiv = (unsigned short)(doubleVCO / (Crystal_Frequency/2));
	xdiv = (unsigned short)(doubleVCO / (CrystalFreqKhz/2));
//	if( (doubleVCO - xdiv * (Crystal_Frequency/2)) >= (Crystal_Frequency/4) )
	if( (doubleVCO - xdiv * (CrystalFreqKhz/2)) >= (CrystalFreqKhz/4) )
	{
		xdiv = xdiv + 1;
	}

	pm = (unsigned char)( xdiv / 8 );
    am = (unsigned char)( xdiv - (8 * pm));

    if (am < 2)
    {
        reg[1] = am + 8;
        reg[2] = pm - 1;
    }
    else
    {
        reg[1] = am;
        reg[2] = pm;
    }

//	xin = (unsigned short)(doubleVCO - ((unsigned short)(doubleVCO / (Crystal_Frequency/2))) * (Crystal_Frequency/2));
	xin = (unsigned short)(doubleVCO - ((unsigned short)(doubleVCO / (CrystalFreqKhz/2))) * (CrystalFreqKhz/2));
//	xin = ((xin << 15)/(Crystal_Frequency/2));
	xin = (unsigned short)((xin << 15)/(CrystalFreqKhz/2));

//	if( xin >= (unsigned short) pow( (double)2, (double)14) )
//	{
//		xin = xin + (unsigned short) pow( (double)2, (double)15);
//	}
	if( xin >= (unsigned short) 16384 )
		xin = xin + (unsigned short) 32768;

	reg[3] = (unsigned char)(xin >> 8);
	reg[4] = (unsigned char)(xin & 0x00FF);


	//===================================== Only for testing
//	printf("Frequency: %d, Fa: %d, Fp: %d, Xin:%d \n", Frequency, am, pm, xin);


	// Set Low-Pass Filter Bandwidth.
    switch(Bandwidth)
    {
        case 6:
			reg[6] = 0x80 | reg[6];
            break;
        case 7:
			reg[6] = ~0x80 & reg[6];
            reg[6] = 0x40 | reg[6];
            break;
        case 8:
        default:
			reg[6] = ~0xC0 & reg[6];
            break;
    }

	reg[5] = reg[5] | 0x07;

	if(FC0013_Write(pTuner, 0x01, reg[1]) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x02, reg[2]) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x03, reg[3]) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x04, reg[4]) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x05, reg[5]) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x06, reg[6]) != FC0013_I2C_SUCCESS) goto error_status;

	if (multi == 64)
	{
//		FC0013_Write(0x11, FC0013_Read(0x11) | 0x04);
		if(FC0013_Read(pTuner, 0x11, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x11, read_byte | 0x04) != FC0013_I2C_SUCCESS) goto error_status;
	}
	else
	{
//		FC0013_Write(0x11, FC0013_Read(0x11) & 0xFB);
		if(FC0013_Read(pTuner, 0x11, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
		if(FC0013_Write(pTuner, 0x11, read_byte & 0xFB) != FC0013_I2C_SUCCESS) goto error_status;
	}

	if(FC0013_Write(pTuner, 0x0E, 0x80) != FC0013_I2C_SUCCESS) goto error_status;
	if(FC0013_Write(pTuner, 0x0E, 0x00) != FC0013_I2C_SUCCESS) goto error_status;

	if(FC0013_Write(pTuner, 0x0E, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
//	reg[14] = 0x3F & FC0013_Read(0x0E);
	if(FC0013_Read(pTuner, 0x0E, &read_byte) != FC0013_I2C_SUCCESS) goto error_status;
	reg[14] = 0x3F & read_byte;

	if (VCO1)
    {
        if (reg[14] > 0x3C)
        {
            reg[6] = ~0x08 & reg[6];

			if(FC0013_Write(pTuner, 0x06, reg[6]) != FC0013_I2C_SUCCESS) goto error_status;

			if(FC0013_Write(pTuner, 0x0E, 0x80) != FC0013_I2C_SUCCESS) goto error_status;
			if(FC0013_Write(pTuner, 0x0E, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
        }
    }
	else
    {
        if (reg[14] < 0x02)
        {
            reg[6] = 0x08 | reg[6];

			if(FC0013_Write(pTuner, 0x06, reg[6]) != FC0013_I2C_SUCCESS) goto error_status;

			if(FC0013_Write(pTuner, 0x0E, 0x80) != FC0013_I2C_SUCCESS) goto error_status;
			if(FC0013_Write(pTuner, 0x0E, 0x00) != FC0013_I2C_SUCCESS) goto error_status;
        }
    }


	return 1;

error_status:
	return 0;
}

