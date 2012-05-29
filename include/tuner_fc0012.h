/*
 * Fitipower FC0012 tuner driver
 *
 * Copyright (C) 2012 Hans-Frieder Vogt <hfvogt@gmx.net>
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

#ifndef _FC0012_H_
#define _FC0012_H_

#define FC0012_I2C_ADDR		0xc6
#define FC0012_CHECK_ADDR	0x00
#define FC0012_CHECK_VAL	0xa1

int fc0012_init(void *dev);
int fc0012_set_params(void *dev, uint32_t freq, uint32_t bandwidth);
int fc0012_set_gain(void *dev, int gain);

#endif
