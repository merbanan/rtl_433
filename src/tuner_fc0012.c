/*
 * Fitipower FC0012 tuner driver
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 *
 * modified for use in librtlsdr
 * Copyright (C) 2012 Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdint.h>
#include <stdio.h>

#include "rtlsdr_i2c.h"
#include "tuner_fc0012.h"

static int fc0012_writereg(void *dev, uint8_t reg, uint8_t val)
{
	uint8_t data[2];
	data[0] = reg;
	data[1] = val;

	if (rtlsdr_i2c_write_fn(dev, FC0012_I2C_ADDR, data, 2) < 0)
		return -1;

	return 0;
}

static int fc0012_readreg(void *dev, uint8_t reg, uint8_t *val)
{
	uint8_t data = reg;

	if (rtlsdr_i2c_write_fn(dev, FC0012_I2C_ADDR, &data, 1) < 0)
		return -1;

	if (rtlsdr_i2c_read_fn(dev, FC0012_I2C_ADDR, &data, 1) < 0)
		return -1;

	*val = data;

	return 0;
}

/* Incomplete list of register settings:
 *
 * Name			Reg	Bits	Desc
 * CHIP_ID		0x00	0-7	Chip ID (constant 0xA1)
 * RF_A			0x01	0-3	Number of count-to-9 cycles in RF
 *					divider (suggested: 2..9)
 * RF_M			0x02	0-7	Total number of cycles (to-8 and to-9)
 *					in RF divider
 * RF_K_HIGH		0x03	0-6	Bits 8..14 of fractional divider
 * RF_K_LOW		0x04	0-7	Bits 0..7 of fractional RF divider
 * RF_OUTDIV_A		0x05	3-7	Power of two required?
 * LNA_POWER_DOWN	0x06	0	Set to 1 to switch off low noise amp
 * RF_OUTDIV_B		0x06	1	Set to select 3 instead of 2 for the
 *					RF output divider
 * VCO_SPEED		0x06	3	Select tuning range of VCO:
 *					 0 = Low range, (ca. 1.1 - 1.5GHz)
 *					 1 = High range (ca. 1.4 - 1.8GHz)
 * BANDWIDTH		0x06	6-7	Set bandwidth. 6MHz = 0x80, 7MHz=0x40
 *					8MHz=0x00
 * XTAL_SPEED		0x07	5	Set to 1 for 28.8MHz Crystal input
 *					or 0 for 36MHz
 * <agc params>		0x08	0-7
 * EN_CAL_RSSI		0x09	4 	Enable calibrate RSSI
 *					(Receive Signal Strength Indicator)
 * LNA_FORCE		0x0d	0
 * AGC_FORCE		0x0d	?
 * LNA_GAIN		0x13	3-4	Low noise amp gain
 * LNA_COMPS		0x15	3	?
 * VCO_CALIB		0x0e	7	Set high then low to calibrate VCO
 *					 (fast lock?)
 * VCO_VOLTAGE		0x0e	0-6	Read Control voltage of VCO
 *					 (big value -> low freq)
 */

int fc0012_init(void *dev)
{
	int ret = 0;
	unsigned int i;
	uint8_t reg[] = {
		0x00,	/* dummy reg. 0 */
		0x05,	/* reg. 0x01 */
		0x10,	/* reg. 0x02 */
		0x00,	/* reg. 0x03 */
		0x00,	/* reg. 0x04 */
		0x0f,	/* reg. 0x05: may also be 0x0a */
		0x00,	/* reg. 0x06: divider 2, VCO slow */
		0x00,	/* reg. 0x07: may also be 0x0f */
		0xff,	/* reg. 0x08: AGC Clock divide by 256, AGC gain 1/256,
			   Loop Bw 1/8 */
		0x6e,	/* reg. 0x09: Disable LoopThrough, Enable LoopThrough: 0x6f */
		0xb8,	/* reg. 0x0a: Disable LO Test Buffer */
		0x82,	/* reg. 0x0b: Output Clock is same as clock frequency,
			   may also be 0x83 */
		0xfc,	/* reg. 0x0c: depending on AGC Up-Down mode, may need 0xf8 */
		0x02,	/* reg. 0x0d: AGC Not Forcing & LNA Forcing, 0x02 for DVB-T */
		0x00,	/* reg. 0x0e */
		0x00,	/* reg. 0x0f */
		0x00,	/* reg. 0x10: may also be 0x0d */
		0x00,	/* reg. 0x11 */
		0x1f,	/* reg. 0x12: Set to maximum gain */
		0x08,	/* reg. 0x13: Set to Middle Gain: 0x08,
			   Low Gain: 0x00, High Gain: 0x10, enable IX2: 0x80 */
		0x00,	/* reg. 0x14 */
		0x04,	/* reg. 0x15: Enable LNA COMPS */
	};

#if 0
	switch (rtlsdr_get_tuner_clock(dev)) {
	case FC_XTAL_27_MHZ:
	case FC_XTAL_28_8_MHZ:
		reg[0x07] |= 0x20;
		break;
	case FC_XTAL_36_MHZ:
	default:
		break;
	}
#endif
	reg[0x07] |= 0x20;

//	if (priv->dual_master)
	reg[0x0c] |= 0x02;

	for (i = 1; i < sizeof(reg); i++) {
		ret = fc0012_writereg(dev, i, reg[i]);
		if (ret)
			break;
	}

	return ret;
}

int fc0012_set_params(void *dev, uint32_t freq, uint32_t bandwidth)
{
	int i, ret = 0;
	uint8_t reg[7], am, pm, multi, tmp;
	uint64_t f_vco;
	uint32_t xtal_freq_div_2;
	uint16_t xin, xdiv;
	int vco_select = 0;

	xtal_freq_div_2 = rtlsdr_get_tuner_clock(dev) / 2;

	/* select frequency divider and the frequency of VCO */
	if (freq < 37084000) {		/* freq * 96 < 3560000000 */
		multi = 96;
		reg[5] = 0x82;
		reg[6] = 0x00;
	} else if (freq < 55625000) {	/* freq * 64 < 3560000000 */
		multi = 64;
		reg[5] = 0x82;
		reg[6] = 0x02;
	} else if (freq < 74167000) {	/* freq * 48 < 3560000000 */
		multi = 48;
		reg[5] = 0x42;
		reg[6] = 0x00;
	} else if (freq < 111250000) {	/* freq * 32 < 3560000000 */
		multi = 32;
		reg[5] = 0x42;
		reg[6] = 0x02;
	} else if (freq < 148334000) {	/* freq * 24 < 3560000000 */
		multi = 24;
		reg[5] = 0x22;
		reg[6] = 0x00;
	} else if (freq < 222500000) {	/* freq * 16 < 3560000000 */
		multi = 16;
		reg[5] = 0x22;
		reg[6] = 0x02;
	} else if (freq < 296667000) {	/* freq * 12 < 3560000000 */
		multi = 12;
		reg[5] = 0x12;
		reg[6] = 0x00;
	} else if (freq < 445000000) {	/* freq * 8 < 3560000000 */
		multi = 8;
		reg[5] = 0x12;
		reg[6] = 0x02;
	} else if (freq < 593334000) {	/* freq * 6 < 3560000000 */
		multi = 6;
		reg[5] = 0x0a;
		reg[6] = 0x00;
	} else {
		multi = 4;
		reg[5] = 0x0a;
		reg[6] = 0x02;
	}

	f_vco = freq * multi;

	if (f_vco >= 3060000000U) {
		reg[6] |= 0x08;
		vco_select = 1;
	}

	/* From divided value (XDIV) determined the FA and FP value */
	xdiv = (uint16_t)(f_vco / xtal_freq_div_2);
	if ((f_vco - xdiv * xtal_freq_div_2) >= (xtal_freq_div_2 / 2))
		xdiv++;

	pm = (uint8_t)(xdiv / 8);
	am = (uint8_t)(xdiv - (8 * pm));

	if (am < 2) {
		am += 8;
		pm--;
	}

	if (pm > 31) {
		reg[1] = am + (8 * (pm - 31));
		reg[2] = 31;
	} else {
		reg[1] = am;
		reg[2] = pm;
	}

	if ((reg[1] > 15) || (reg[2] < 0x0b)) {
		fprintf(stderr, "[FC0012] no valid PLL combination "
				"found for %u Hz!\n", freq);
		return -1;
	}

	/* fix clock out */
	reg[6] |= 0x20;

	/* From VCO frequency determines the XIN ( fractional part of Delta
	   Sigma PLL) and divided value (XDIV) */
	xin = (uint16_t)((f_vco - (f_vco / xtal_freq_div_2) * xtal_freq_div_2) / 1000);
	xin = (xin << 15) / (xtal_freq_div_2 / 1000);
	if (xin >= 16384)
		xin += 32768;

	reg[3] = xin >> 8;	/* xin with 9 bit resolution */
	reg[4] = xin & 0xff;

	reg[6] &= 0x3f; /* bits 6 and 7 describe the bandwidth */
	switch (bandwidth) {
	case 6000000:
		reg[6] |= 0x80;
		break;
	case 7000000:
		reg[6] |= 0x40;
		break;
	case 8000000:
	default:
		break;
	}

	/* modified for Realtek demod */
	reg[5] |= 0x07;

	for (i = 1; i <= 6; i++) {
		ret = fc0012_writereg(dev, i, reg[i]);
		if (ret)
			goto exit;
	}

	/* VCO Calibration */
	ret = fc0012_writereg(dev, 0x0e, 0x80);
	if (!ret)
		ret = fc0012_writereg(dev, 0x0e, 0x00);

	/* VCO Re-Calibration if needed */
	if (!ret)
		ret = fc0012_writereg(dev, 0x0e, 0x00);

	if (!ret) {
//		msleep(10);
		ret = fc0012_readreg(dev, 0x0e, &tmp);
	}
	if (ret)
		goto exit;

	/* vco selection */
	tmp &= 0x3f;

	if (vco_select) {
		if (tmp > 0x3c) {
			reg[6] &= ~0x08;
			ret = fc0012_writereg(dev, 0x06, reg[6]);
			if (!ret)
				ret = fc0012_writereg(dev, 0x0e, 0x80);
			if (!ret)
				ret = fc0012_writereg(dev, 0x0e, 0x00);
		}
	} else {
		if (tmp < 0x02) {
			reg[6] |= 0x08;
			ret = fc0012_writereg(dev, 0x06, reg[6]);
			if (!ret)
				ret = fc0012_writereg(dev, 0x0e, 0x80);
			if (!ret)
				ret = fc0012_writereg(dev, 0x0e, 0x00);
		}
	}

exit:
	return ret;
}

int fc0012_set_gain(void *dev, int gain)
{
	int ret;
	uint8_t tmp = 0;

	ret = fc0012_readreg(dev, 0x13, &tmp);

	/* mask bits off */
	tmp &= 0xe0;

	switch (gain) {
	case -99:		/* -9.9 dB */
		tmp |= 0x02;
		break;
	case -40:		/* -4 dB */
		break;
	case 71:
		tmp |= 0x08;	/* 7.1 dB */
		break;
	case 179:
		tmp |= 0x17;	/* 17.9 dB */
		break;
	case 192:
	default:
		tmp |= 0x10;	/* 19.2 dB */
		break;
	}

	ret = fc0012_writereg(dev, 0x13, tmp);

	return ret;
}
