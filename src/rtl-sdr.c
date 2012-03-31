/*
 * rtl-sdr, a poor man's SDR using a Realtek RTL2832 based DVB-stick
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 *(at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

#include <libusb.h>

#include "tuner_e4000.h"
#include "tuner_fc0013.h"

/* generic tuner interface functions, shall be moved to the tuner implementations */
int e4k_init(void *dev) { return e4000_Initialize(dev); }
int e4k_exit(void *dev) { return 0; }
int e4k_tune(void *dev, int freq) { return e4000_SetRfFreqHz(dev, freq); }
int e4k_set_bw(void *dev, int bw) { return e4000_SetBandwidthHz(dev, 8000000); }

int fc0012_init(void *dev) { return 0; }
int fc0012_exit(void *dev) { return 0; }
int fc0012_tune(void *dev, int freq) { return 0; }
int fc0012_set_bw(void *dev, int bw) { return 0; }

int fc0013_init(void *dev) { return FC0013_Open(dev); }
int fc0013_exit(void *dev) { return 0; }
int fc0013_tune(void *dev, int freq) {
	/* read bandwidth mode to reapply it */
	int bw = 0;
	//fc0013_GetBandwidthMode(dev, &bw); // FIXME: missing
	return FC0013_SetFrequency(dev, freq/1000, bw & 0xff);
}

int fc0013_set_bw(void *dev, int bw) {
	/* read frequency to reapply it */
	unsigned long freq = 0;
	//fc0013_GetRfFreqHz(dev, &freq); // FIXME: missing
	return FC0013_SetFrequency(dev, freq/1000, 8);
}

enum rtlsdr_tuners {
	RTLSDR_TUNER_UNDEF,
	RTLSDR_TUNER_E4000,
	RTLSDR_TUNER_FC0012,
	RTLSDR_TUNER_FC0013
};

typedef struct rtlsdr_tuner {
	enum rtlsdr_tuners tuner;
	int(*init)(void *);
	int(*exit)(void *);
	int(*tune)(void *, int freq /* Hz */);
	int(*set_bw)(void *, int bw /* Hz */);
	int freq; /* Hz */
	int corr; /* ppm */
} rtlsdr_tuner_t;

rtlsdr_tuner_t tuners[] = {
	{ RTLSDR_TUNER_E4000, e4k_init, e4k_exit, e4k_tune, e4k_set_bw, 0, 0 },
	{ RTLSDR_TUNER_FC0012, fc0012_init, fc0012_exit, fc0012_tune, fc0012_set_bw, 0, 0 },
	{ RTLSDR_TUNER_FC0013, fc0013_init, fc0013_exit, fc0013_tune, fc0013_set_bw, 0, 0 },
};

struct rtlsdr_device {
	uint16_t vid;
	uint16_t pid;
} devices[] = {
	{ 0x0bda, 0x2832 }, /* default RTL2832U vid/pid (eg. hama nano) */
	{ 0x0bda, 0x2838 }, /* ezcap USB 2.0 DVB-T/DAB/FM stick */
	{ 0x0ccd, 0x00b3 }, /* Terratec NOXON DAB/DAB+ USB-Stick rev 1 */
	{ 0x1f4d, 0xb803 }, /* GTek T803 */
	{ 0x1b80, 0xd3a4 }, /* Twintech UT-40 */
	{ 0x1d19, 0x1101 }, /* Dexatek DK DVB-T Dongle (Logilink VG0002A) */
};

typedef struct {
	struct libusb_device_handle *devh;
	rtlsdr_tuner_t *tuner;
} rtlsdr_dev_t;

#define CRYSTAL_FREQ	28800000

#define CTRL_IN		(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRL_OUT	(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)

enum usb_reg {
	USB_SYSCTL		= 0x2000,
	USB_CTRL		= 0x2010,
	USB_STAT		= 0x2014,
	USB_EPA_CFG		= 0x2144,
	USB_EPA_CTL		= 0x2148,
	USB_EPA_MAXPKT		= 0x2158,
	USB_EPA_MAXPKT_2	= 0x215a,
	USB_EPA_FIFO_CFG	= 0x2160,
};

enum sys_reg {
	DEMOD_CTL		= 0x3000,
	GPO			= 0x3001,
	GPI			= 0x3002,
	GPOE			= 0x3003,
	GPD			= 0x3004,
	SYSINTE			= 0x3005,
	SYSINTS			= 0x3006,
	GP_CFG0			= 0x3007,
	GP_CFG1			= 0x3008,
	SYSINTE_1		= 0x3009,
	SYSINTS_1		= 0x300a,
	DEMOD_CTL_1		= 0x300b,
	IR_SUSPEND		= 0x300c,
};

enum blocks {
	DEMODB			= 0,
	USBB			= 1,
	SYSB			= 2,
	TUNB			= 3,
	ROMB			= 4,
	IRB			= 5,
	IICB			= 6,
};

int rtlsdr_read_array(rtlsdr_dev_t *dev, uint8_t block, uint16_t addr, uint8_t *array, uint8_t len)
{
	int r;
	uint16_t index = (block << 8);

	r = libusb_control_transfer(dev->devh, CTRL_IN, 0, addr, index, array, len, 0);

	return r;
}

int rtlsdr_write_array(rtlsdr_dev_t *dev, uint8_t block, uint16_t addr, uint8_t *array, uint8_t len)
{
	int r;
	uint16_t index = (block << 8) | 0x10;

	r = libusb_control_transfer(dev->devh, CTRL_OUT, 0, addr, index, array, len, 0);

	return r;
}

int rtlsdr_i2c_write(rtlsdr_dev_t *dev, uint8_t i2c_addr, uint8_t *buffer, int len)
{
	uint16_t addr = i2c_addr;
	return rtlsdr_write_array(dev, IICB, addr, buffer, len);
}

int rtlsdr_i2c_read(rtlsdr_dev_t *dev, uint8_t i2c_addr, uint8_t *buffer, int len)
{
	uint16_t addr = i2c_addr;
	return rtlsdr_read_array(dev, IICB, addr, buffer, len);
}

uint16_t rtlsdr_read_reg(rtlsdr_dev_t *dev, uint8_t block, uint16_t addr, uint8_t len)
{
	int r;
	unsigned char data[2];
	uint16_t index = (block << 8);
	uint16_t reg;

	r = libusb_control_transfer(dev->devh, CTRL_IN, 0, addr, index, data, len, 0);

	if (r < 0)
		printf("%s failed\n", __FUNCTION__);

	reg = (data[1] << 8) | data[0];

	return reg;
}

void rtlsdr_write_reg(rtlsdr_dev_t *dev, uint8_t block, uint16_t addr, uint16_t val, uint8_t len)
{
	int r;
	unsigned char data[2];

	uint16_t index = (block << 8) | 0x10;

	if (len == 1)
		data[0] = val & 0xff;
	else
		data[0] = val >> 8;

	data[1] = val & 0xff;

	r = libusb_control_transfer(dev->devh, CTRL_OUT, 0, addr, index, data, len, 0);

	if (r < 0)
		printf("%s failed\n", __FUNCTION__);
}

uint16_t rtlsdr_demod_read_reg(rtlsdr_dev_t *dev, uint8_t page, uint8_t addr, uint8_t len)
{
	int r;
	unsigned char data[2];

	uint16_t index = page;
	uint16_t reg;
	addr = (addr << 8) | 0x20;

	r = libusb_control_transfer(dev->devh, CTRL_IN, 0, addr, index, data, len, 0);

	if (r < 0)
		printf("%s failed\n", __FUNCTION__);

	reg = (data[1] << 8) | data[0];

	return reg;
}

void rtlsdr_demod_write_reg(rtlsdr_dev_t *dev, uint8_t page, uint16_t addr, uint16_t val, uint8_t len)
{
	int r;
	unsigned char data[2];
	uint16_t index = 0x10 | page;
	addr = (addr << 8) | 0x20;

	if (len == 1)
		data[0] = val & 0xff;
	else
		data[0] = val >> 8;

	data[1] = val & 0xff;

	r = libusb_control_transfer(dev->devh, CTRL_OUT, 0, addr, index, data, len, 0);

	if (r < 0)
		printf("%s failed\n", __FUNCTION__);

	rtlsdr_demod_read_reg(dev, 0x0a, 0x01, 1);
}

void rtlsdr_set_i2c_repeater(rtlsdr_dev_t *dev, int on)
{
	rtlsdr_demod_write_reg(dev, 1, 0x01, on ? 0x18 : 0x10, 1);
}

void rtlsdr_init_baseband(rtlsdr_dev_t *dev)
{
	unsigned int i;

	/* default FIR coefficients used for DAB/FM by the Windows driver,
	 * the DVB driver uses different ones */
	uint8_t fir_coeff[] = {
		0xca, 0xdc, 0xd7, 0xd8, 0xe0, 0xf2, 0x0e, 0x35, 0x06, 0x50,
		0x9c, 0x0d, 0x71, 0x11, 0x14, 0x71, 0x74, 0x19, 0x41, 0x00,
	};

	/* initialize USB */
	rtlsdr_write_reg(dev, USBB, USB_SYSCTL, 0x09, 1);
	rtlsdr_write_reg(dev, USBB, USB_EPA_MAXPKT, 0x0002, 2);
	rtlsdr_write_reg(dev, USBB, USB_EPA_CTL, 0x1002, 2);

	/* poweron demod */
	rtlsdr_write_reg(dev, SYSB, DEMOD_CTL_1, 0x22, 1);
	rtlsdr_write_reg(dev, SYSB, DEMOD_CTL, 0xe8, 1);

	/* reset demod (bit 3, soft_rst) */
	rtlsdr_demod_write_reg(dev, 1, 0x01, 0x14, 1);
	rtlsdr_demod_write_reg(dev, 1, 0x01, 0x10, 1);

	/* disable spectrum inversion and adjacent channel rejection */
	rtlsdr_demod_write_reg(dev, 1, 0x15, 0x00, 1);
	rtlsdr_demod_write_reg(dev, 1, 0x16, 0x0000, 2);

	/* set IF-frequency to 0 Hz */
	rtlsdr_demod_write_reg(dev, 1, 0x19, 0x0000, 2);

	/* set FIR coefficients */
	for (i = 0; i < sizeof (fir_coeff); i++)
		rtlsdr_demod_write_reg(dev, 1, 0x1c + i, fir_coeff[i], 1);

	rtlsdr_demod_write_reg(dev, 0, 0x19, 0x25, 1);

	/* init FSM state-holding register */
	rtlsdr_demod_write_reg(dev, 1, 0x93, 0xf0, 1);

	/* disable AGC (en_dagc, bit 0) */
	rtlsdr_demod_write_reg(dev, 1, 0x11, 0x00, 1);

	/* disable PID filter (enable_PID = 0) */
	rtlsdr_demod_write_reg(dev, 0, 0x61, 0x60, 1);

	/* opt_adc_iq = 0, default ADC_I/ADC_Q datapath */
	rtlsdr_demod_write_reg(dev, 0, 0x06, 0x80, 1);

	/* Enable Zero-IF mode (en_bbin bit), DC cancellation (en_dc_est),
	 * IQ estimation/compensation (en_iq_comp, en_iq_est) */
	rtlsdr_demod_write_reg(dev, 1, 0xb1, 0x1b, 1);
}

int rtlsdr_set_center_freq(rtlsdr_dev_t *dev, uint32_t freq)
{
	rtlsdr_set_i2c_repeater(dev, 1);

	if (dev->tuner) {
		dev->tuner->freq = freq;
		double f = (double) freq;
		f *= 1.0 + dev->tuner->corr / 1e6;
		dev->tuner->tune((void *)dev, (int) f);
		printf("Tuned to %i Hz\n", freq);
	}

	rtlsdr_set_i2c_repeater(dev, 0);

	return 0;
}

int rtlsdr_get_center_freq(rtlsdr_dev_t *dev)
{
	return 0; // TODO: implement
}

int rtlsdr_set_freq_correction(rtlsdr_dev_t *dev, int32_t ppm)
{
	if (dev->tuner) {
		if (dev->tuner->corr == ppm)
			return -1;

		dev->tuner->corr = ppm;

		/* retune to apply new correction value */
		rtlsdr_set_center_freq(dev, dev->tuner->freq);
	}

	return 0;
}

int32_t rtlsdr_get_freq_correction(rtlsdr_dev_t *dev)
{
	if (dev->tuner)
		return dev->tuner->corr;
	else
		return 0;
}

void rtlsdr_set_sample_rate(rtlsdr_dev_t *dev, uint32_t samp_rate)
{
	uint16_t tmp;
	uint32_t rsamp_ratio;
	double real_rate;

	/* check for the maximum rate the resampler supports */
	if (samp_rate > 3200000)
		samp_rate = 3200000;

	rsamp_ratio = (CRYSTAL_FREQ * pow(2, 22)) / samp_rate;
	rsamp_ratio &= ~3;

	real_rate = (CRYSTAL_FREQ * pow(2, 22)) / rsamp_ratio;
	printf("Setting sample rate: %.3f Hz\n", real_rate);

	if (dev->tuner)
		dev->tuner->set_bw((void *)dev, real_rate);

	tmp = (rsamp_ratio >> 16);
	rtlsdr_demod_write_reg(dev, 1, 0x9f, tmp, 2);
	tmp = rsamp_ratio & 0xffff;
	rtlsdr_demod_write_reg(dev, 1, 0xa1, tmp, 2);
}

int rtlsdr_get_sample_rate(rtlsdr_dev_t *dev)
{
	return 0; // TODO: implement
}

int rtlsdr_init(void)
{
	return libusb_init(NULL);
}

void rtlsdr_exit(void)
{
	libusb_exit(NULL);
}

uint32_t rtlsdr_get_device_count(void)
{
	int i, j;
	libusb_device **list;
	uint32_t device_count = 0;
	struct libusb_device_descriptor dd;

	ssize_t cnt = libusb_get_device_list(NULL, &list);

	for (i = 0; i < cnt; i++) {
		libusb_get_device_descriptor(list[i], &dd);

		for (j = 0; j < sizeof(devices)/sizeof(struct rtlsdr_device); j++ ) {
			if (devices[j].vid == dd.idVendor && devices[j].pid == dd.idProduct)
				device_count++;
		}
	}

	libusb_free_device_list(list, 0);

	return device_count;
}

const char *rtlsdr_get_device_name(uint32_t index)
{
	libusb_device **list;

	ssize_t cnt = libusb_get_device_list(NULL, &list);

	if (index > cnt - 1)
		return NULL;

	/*libusb_device *device = list[index];*/



	libusb_free_device_list(list, 0);

	return "TODO: implement";
}

rtlsdr_dev_t *rtlsdr_open(int index)
{
	int r;
	int i, j;
	libusb_device **list;
	rtlsdr_dev_t * dev = NULL;
	libusb_device *device = NULL;
	uint32_t device_count = 0;
	struct libusb_device_descriptor dd;

	dev = malloc(sizeof(rtlsdr_dev_t));
	memset(dev, 0, sizeof(rtlsdr_dev_t));

	ssize_t cnt = libusb_get_device_list(NULL, &list);

	for (i = 0; i < cnt; i++) {
		device = list[i];

		libusb_get_device_descriptor(list[i], &dd);

		for (j = 0; j < sizeof(devices)/sizeof(struct rtlsdr_device); j++ ) {
			if ( devices[j].vid == dd.idVendor && devices[j].pid == dd.idProduct ) {
				device_count++;
				if (index == device_count - 1)
					break;
			}
		}

		if (index == device_count - 1)
			break;

		device = NULL;
		}

		if (!device)
			goto err;

		r = libusb_open(device, &dev->devh);
		if (r < 0) {
			libusb_free_device_list(list, 0);
			fprintf(stderr, "usb_open error %d\n", r);
			goto err;
		}

		libusb_free_device_list(list, 0);

		unsigned char buffer[256];

		libusb_get_string_descriptor_ascii(dev->devh, 0, buffer, sizeof(buffer));
		printf("sn#: %s\n", buffer);

		libusb_get_string_descriptor_ascii(dev->devh, 1, buffer, sizeof(buffer));
		printf("manufacturer: %s\n", buffer);

		libusb_get_string_descriptor_ascii(dev->devh, 2, buffer, sizeof(buffer));
		printf("product: %s\n", buffer);

		r = libusb_claim_interface(dev->devh, 0);
		if (r < 0) {
			fprintf(stderr, "usb_claim_interface error %d\n", r);
			goto err;
		}

	rtlsdr_init_baseband(dev);

	// TODO: probe the tuner and set dev->tuner member to appropriate tuner object
	// dev->tuner = &tuners[...];

	return dev;
err:
	return NULL;
}

int rtlsdr_close(rtlsdr_dev_t *dev)
{
	libusb_release_interface(dev->devh, 0);
	libusb_close(dev->devh);
	free(dev);

	return 0;
}

int rtlsdr_reset_buffer(rtlsdr_dev_t *dev)
{
	return 0; // TODO: implement
}

int rtlsdr_read_sync(rtlsdr_dev_t *dev, void *buf, int len, int *n_read)
{
	return libusb_bulk_transfer(dev->devh, 0x81, buf, len, n_read, 3000);
}
#if 0
int rtlsdr_async_loop(rtlsdr_dev_t *dev, rtlsdr_async_read_cb_t cb, void *ctx)
{
	return 0;
}
#endif
