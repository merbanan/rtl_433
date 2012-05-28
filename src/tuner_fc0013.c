/*
 * Fitipower FC0013 tuner driver
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
 * partially based on driver code from Fitipower
 * Copyright (C) 2010 Fitipower Integrated Technology Inc
 *
 * modified for use in librtlsdr
 * Copyright (C) 2012 Steve Markgraf <steve@steve-m.de>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdint.h>

#include "rtlsdr_i2c.h"
#include "tuner_fc0013.h"

static int fc0013_writereg(void *dev, uint8_t reg, uint8_t val)
{
	uint8_t data[2];
	data[0] = reg;
	data[1] = val;

	if (rtlsdr_i2c_write_fn(dev, FC0013_I2C_ADDR, data, 2) < 0)
		return -1;

	return 0;
}

static int fc0013_readreg(void *dev, uint8_t reg, uint8_t *val)
{
	uint8_t data = reg;

	if (rtlsdr_i2c_write_fn(dev, FC0013_I2C_ADDR, &data, 1) < 0)
		return -1;

	if (rtlsdr_i2c_read_fn(dev, FC0013_I2C_ADDR, &data, 1) < 0)
		return -1;

	*val = data;

	return 0;
}

int fc0013_init(void *dev)
{
	int ret = 0;
	unsigned int i;
	uint8_t reg[] = {
		0x00,	/* reg. 0x00: dummy */
		0x09,	/* reg. 0x01 */
		0x16,	/* reg. 0x02 */
		0x00,	/* reg. 0x03 */
		0x00,	/* reg. 0x04 */
		0x17,	/* reg. 0x05 */
		0x02,	/* reg. 0x06: LPF bandwidth */
		0x0a,	/* reg. 0x07: CHECK */
		0xff,	/* reg. 0x08: AGC Clock divide by 256, AGC gain 1/256,
			   Loop Bw 1/8 */
		0x6f,	/* reg. 0x09: enable LoopThrough */
		0xb8,	/* reg. 0x0a: Disable LO Test Buffer */
		0x82,	/* reg. 0x0b: CHECK */
		0xfc,	/* reg. 0x0c: depending on AGC Up-Down mode, may need 0xf8 */
		0x01,	/* reg. 0x0d: AGC Not Forcing & LNA Forcing, may need 0x02 */
		0x00,	/* reg. 0x0e */
		0x00,	/* reg. 0x0f */
		0x00,	/* reg. 0x10 */
		0x00,	/* reg. 0x11 */
		0x00,	/* reg. 0x12 */
		0x00,	/* reg. 0x13 */
		0x50,	/* reg. 0x14: DVB-t High Gain, UHF.
			   Middle Gain: 0x48, Low Gain: 0x40 */
		0x01,	/* reg. 0x15 */
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

//	if (dev->dual_master)
	reg[0x0c] |= 0x02;

	for (i = 1; i < sizeof(reg); i++) {
		ret = fc0013_writereg(dev, i, reg[i]);
		if (ret < 0)
			break;
	}

	return ret;
}

int fc0013_rc_cal_add(void *dev, int rc_val)
{
	int ret;
	uint8_t rc_cal;
	int val;

	/* push rc_cal value, get rc_cal value */
	ret = fc0013_writereg(dev, 0x10, 0x00);
	if (ret)
		goto error_out;

	/* get rc_cal value */
	ret = fc0013_readreg(dev, 0x10, &rc_cal);
	if (ret)
		goto error_out;

	rc_cal &= 0x0f;

	val = (int)rc_cal + rc_val;

	/* forcing rc_cal */
	ret = fc0013_writereg(dev, 0x0d, 0x11);
	if (ret)
		goto error_out;

	/* modify rc_cal value */
	if (val > 15)
		ret = fc0013_writereg(dev, 0x10, 0x0f);
	else if (val < 0)
		ret = fc0013_writereg(dev, 0x10, 0x00);
	else
		ret = fc0013_writereg(dev, 0x10, (uint8_t)val);

error_out:
	return ret;
}

int fc0013_rc_cal_reset(void *dev)
{
	int ret;

	ret = fc0013_writereg(dev, 0x0d, 0x01);
	if (!ret)
		ret = fc0013_writereg(dev, 0x10, 0x00);

	return ret;
}

static int fc0013_set_vhf_track(void *dev, uint32_t freq)
{
	int ret;
	uint8_t tmp;

	ret = fc0013_readreg(dev, 0x1d, &tmp);
	if (ret)
		goto error_out;
	tmp &= 0xe3;
	if (freq <= 177500000) {		/* VHF Track: 7 */
		ret = fc0013_writereg(dev, 0x1d, tmp | 0x1c);
	} else if (freq <= 184500000) {	/* VHF Track: 6 */
		ret = fc0013_writereg(dev, 0x1d, tmp | 0x18);
	} else if (freq <= 191500000) {	/* VHF Track: 5 */
		ret = fc0013_writereg(dev, 0x1d, tmp | 0x14);
	} else if (freq <= 198500000) {	/* VHF Track: 4 */
		ret = fc0013_writereg(dev, 0x1d, tmp | 0x10);
	} else if (freq <= 205500000) {	/* VHF Track: 3 */
		ret = fc0013_writereg(dev, 0x1d, tmp | 0x0c);
	} else if (freq <= 219500000) {	/* VHF Track: 2 */
		ret = fc0013_writereg(dev, 0x1d, tmp | 0x08);
	} else if (freq < 300000000) {		/* VHF Track: 1 */
		ret = fc0013_writereg(dev, 0x1d, tmp | 0x04);
	} else {				/* UHF and GPS */
		ret = fc0013_writereg(dev, 0x1d, tmp | 0x1c);
	}

error_out:
	return ret;
}

int fc0013_set_params(void *dev, uint32_t freq, uint32_t bandwidth)
{
	int i, ret = 0;
	uint8_t reg[7], am, pm, multi, tmp;
	uint64_t f_vco;
	uint32_t xtal_freq_div_2;
	uint16_t xin, xdiv;
	int vco_select = 0;

	xtal_freq_div_2 = rtlsdr_get_tuner_clock(dev) / 2;

	/* set VHF track */
	ret = fc0013_set_vhf_track(dev, freq);
	if (ret)
		goto exit;

	if (freq < 300000000) {
		/* enable VHF filter */
		ret = fc0013_readreg(dev, 0x07, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(dev, 0x07, tmp | 0x10);
		if (ret)
			goto exit;

		/* disable UHF & disable GPS */
		ret = fc0013_readreg(dev, 0x14, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(dev, 0x14, tmp & 0x1f);
		if (ret)
			goto exit;
	} else if (freq <= 862000000) {
		/* disable VHF filter */
		ret = fc0013_readreg(dev, 0x07, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(dev, 0x07, tmp & 0xef);
		if (ret)
			goto exit;

		/* enable UHF & disable GPS */
		ret = fc0013_readreg(dev, 0x14, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(dev, 0x14, (tmp & 0x1f) | 0x40);
		if (ret)
			goto exit;
	} else {
		/* disable VHF filter */
		ret = fc0013_readreg(dev, 0x07, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(dev, 0x07, tmp & 0xef);
		if (ret)
			goto exit;

		/* disable UHF & enable GPS */
		ret = fc0013_readreg(dev, 0x14, &tmp);
		if (ret)
			goto exit;
		ret = fc0013_writereg(dev, 0x14, (tmp & 0x1f) | 0x20);
		if (ret)
			goto exit;
	}

	/* select frequency divider and the frequency of VCO */
	if (freq < 37084000) {		/* freq * 96 < 3560000000 */
		multi = 96;
		reg[5] = 0x82;
		reg[6] = 0x00;
	} else if (freq < 55625000) {	/* freq * 64 < 3560000000 */
		multi = 64;
		reg[5] = 0x02;
		reg[6] = 0x02;
	} else if (freq < 74167000) {	/* freq * 48 < 3560000000 */
		multi = 48;
		reg[5] = 0x42;
		reg[6] = 0x00;
	} else if (freq < 111250000) {	/* freq * 32 < 3560000000 */
		multi = 32;
		reg[5] = 0x82;
		reg[6] = 0x02;
	} else if (freq < 148334000) {	/* freq * 24 < 3560000000 */
		multi = 24;
		reg[5] = 0x22;
		reg[6] = 0x00;
	} else if (freq < 222500000) {	/* freq * 16 < 3560000000 */
		multi = 16;
		reg[5] = 0x42;
		reg[6] = 0x02;
	} else if (freq < 296667000) {	/* freq * 12 < 3560000000 */
		multi = 12;
		reg[5] = 0x12;
		reg[6] = 0x00;
	} else if (freq < 445000000) {	/* freq * 8 < 3560000000 */
		multi = 8;
		reg[5] = 0x22;
		reg[6] = 0x02;
	} else if (freq < 593334000) {	/* freq * 6 < 3560000000 */
		multi = 6;
		reg[5] = 0x0a;
		reg[6] = 0x00;
	} else if (freq < 950000000) {	/* freq * 4 < 3800000000 */
		multi = 4;
		reg[5] = 0x12;
		reg[6] = 0x02;
	} else {
		multi = 2;
		reg[5] = 0x0a;
		reg[6] = 0x02;
	}

	f_vco = freq * multi;

	if (f_vco >= 3060000000U) {
		reg[6] |= 0x08;
		vco_select = 1;
	}

	if (freq >= 45000000) {
		/* From divided value (XDIV) determined the FA and FP value */
		xdiv = (uint16_t)(f_vco / xtal_freq_div_2);
		if ((f_vco - xdiv * xtal_freq_div_2) >= (xtal_freq_div_2 / 2))
			xdiv++;

		pm = (uint8_t)(xdiv / 8);
		am = (uint8_t)(xdiv - (8 * pm));

		if (am < 2) {
			reg[1] = am + 8;
			reg[2] = pm - 1;
		} else {
			reg[1] = am;
			reg[2] = pm;
		}
	} else {
		/* fix for frequency less than 45 MHz */
		reg[1] = 0x06;
		reg[2] = 0x11;
	}

	/* fix clock out */
	reg[6] |= 0x20;

	/* From VCO frequency determines the XIN ( fractional part of Delta
	   Sigma PLL) and divided value (XDIV) */
	xin = (uint16_t)((f_vco - (f_vco / xtal_freq_div_2) * xtal_freq_div_2) / 1000);
	xin = (xin << 15) / (xtal_freq_div_2 / 1000);
	if (xin >= 16384)
		xin += 32768;

	reg[3] = xin >> 8;
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
		ret = fc0013_writereg(dev, i, reg[i]);
		if (ret)
			goto exit;
	}

	ret = fc0013_readreg(dev, 0x11, &tmp);
	if (ret)
		goto exit;
	if (multi == 64)
		ret = fc0013_writereg(dev, 0x11, tmp | 0x04);
	else
		ret = fc0013_writereg(dev, 0x11, tmp & 0xfb);
	if (ret)
		goto exit;

	/* VCO Calibration */
	ret = fc0013_writereg(dev, 0x0e, 0x80);
	if (!ret)
		ret = fc0013_writereg(dev, 0x0e, 0x00);

	/* VCO Re-Calibration if needed */
	if (!ret)
		ret = fc0013_writereg(dev, 0x0e, 0x00);

	if (!ret) {
//		msleep(10);
		ret = fc0013_readreg(dev, 0x0e, &tmp);
	}
	if (ret)
		goto exit;

	/* vco selection */
	tmp &= 0x3f;

	if (vco_select) {
		if (tmp > 0x3c) {
			reg[6] &= ~0x08;
			ret = fc0013_writereg(dev, 0x06, reg[6]);
			if (!ret)
				ret = fc0013_writereg(dev, 0x0e, 0x80);
			if (!ret)
				ret = fc0013_writereg(dev, 0x0e, 0x00);
		}
	} else {
		if (tmp < 0x02) {
			reg[6] |= 0x08;
			ret = fc0013_writereg(dev, 0x06, reg[6]);
			if (!ret)
				ret = fc0013_writereg(dev, 0x0e, 0x80);
			if (!ret)
				ret = fc0013_writereg(dev, 0x0e, 0x00);
		}
	}

exit:
	return ret;
}

int fc0013_set_gain(void *dev, int gain)
{
	int ret, gainshift = 0, en_in_chg = 0;
	uint8_t tmp = 0;

	ret = fc0013_readreg(dev, 0x14, &tmp);

	/* mask bits off */
	tmp &= 0xe0;

	switch (gain) {
	case -63:		/* -6.3 dB */
		break;
	case 71:
		tmp |= 0x08;	/* 7.1 dB */
		break;
	case 191:
		gainshift = 1;
		tmp |= 0x11;	/* 19.1 dB */
		break;
	case 197:
	default:
		en_in_chg = 1;
		gainshift = 1;
		tmp |= 0x10;	/* 19.7 dB */
		break;
	}

	/* set gain */
	ret = fc0013_writereg(dev, 0x14, tmp);

	/* set en_in_chg */
	ret = fc0013_readreg(dev, 0x0a, &tmp);

	if (en_in_chg)
		tmp |= 0x20;
	else
		tmp &= ~0x20;

	ret = fc0013_writereg(dev, 0x0a, tmp);

	/* set gain shift */
	ret = fc0013_readreg(dev, 0x07, &tmp);
	tmp &= 0xf0;
	tmp |= gainshift ? 0x0a : 0x07;
	ret = fc0013_writereg(dev, 0x07, tmp);

	return ret;
}
