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

#define READLEN		(16 * 16384)
#define CTRL_IN		(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRL_OUT	(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)

/* ezcap USB 2.0 DVB-T/DAB/FM stick */
#define EZCAP_VID	0x0bda
#define EZCAP_PID	0x2838

/* Terratec NOXON DAB/DAB+ USB-Stick */
#define NOXON_VID	0x0ccd
#define NOXON_PID	0x00b3

#define CRYSTAL_FREQ	28800000

static struct libusb_device_handle *devh = NULL;
static int do_exit = 0;

enum TUNER_TYPE {
	TUNER_E4000,
	TUNER_FC0013
} tuner_type;

static int find_device(void)
{
	devh = libusb_open_device_with_vid_pid(NULL, EZCAP_VID, EZCAP_PID);
	if (devh > 0) {
		tuner_type = TUNER_E4000;
		printf("Found ezcap stick with E4000 tuner\n");
		return 0;
	}

	devh = libusb_open_device_with_vid_pid(NULL, NOXON_VID, NOXON_PID);
	if (devh > 0) {
		tuner_type = TUNER_FC0013;
		printf("Found Terratec NOXON stick with FC0013 tuner\n");
		return 0;
	}

	return -EIO;
}

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

int rtl_read_array(uint8_t block, uint16_t addr, uint8_t *array, uint8_t len)
{
	int r;
	uint16_t index = (block << 8);

	r = libusb_control_transfer(devh, CTRL_IN, 0, addr, index, array, len, 0);

	return r;
}

int rtl_write_array(uint8_t block, uint16_t addr, uint8_t *array, uint8_t len)
{
	int r;
	uint16_t index = (block << 8) | 0x10;

	r = libusb_control_transfer(devh, CTRL_OUT, 0, addr, index, array, len, 0);

	return r;
}

int rtl_i2c_write(uint8_t i2c_addr, uint8_t *buffer, int len)
{
	uint16_t addr = i2c_addr;
	return rtl_write_array(IICB, addr, buffer, len);
}

int rtl_i2c_read(uint8_t i2c_addr, uint8_t *buffer, int len)
{
	uint16_t addr = i2c_addr;
	return rtl_read_array(IICB, addr, buffer, len);
}

uint16_t rtl_read_reg(uint8_t block, uint16_t addr, uint8_t len)
{
	int r;
	unsigned char data[2];
	uint16_t index = (block << 8);
	uint16_t reg;

	r = libusb_control_transfer(devh, CTRL_IN, 0, addr, index, data, len, 0);

	if (r < 0)
		printf("%s failed\n", __FUNCTION__);

	reg = (data[1] << 8) | data[0];

	return reg;
}

void rtl_write_reg(uint8_t block, uint16_t addr, uint16_t val, uint8_t len)
{
	int r;
	unsigned char data[2];

	uint16_t index = (block << 8) | 0x10;

	if (len == 1)
		data[0] = val & 0xff;
	else
		data[0] = val >> 8;

	data[1] = val & 0xff;

	r = libusb_control_transfer(devh, CTRL_OUT, 0, addr, index, data, len, 0);

	if (r < 0)
		printf("%s failed\n", __FUNCTION__);
}

uint16_t demod_read_reg(uint8_t page, uint8_t addr, uint8_t len)
{
	int r;
	unsigned char data[2];

	uint16_t index = page;
	uint16_t reg;
	addr = (addr << 8) | 0x20;

	r = libusb_control_transfer(devh, CTRL_IN, 0, addr, index, data, len, 0);

	if (r < 0)
		printf("%s failed\n", __FUNCTION__);

	reg = (data[1] << 8) | data[0];

	return reg;
}

void demod_write_reg(uint8_t page, uint16_t addr, uint16_t val, uint8_t len)
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

	r = libusb_control_transfer(devh, CTRL_OUT, 0, addr, index, data, len, 0);

	if (r < 0)
		printf("%s failed\n", __FUNCTION__);

	demod_read_reg(0x0a, 0x01, 1);
}

void set_samp_rate(uint32_t samp_rate)
{
	uint16_t tmp;
	uint32_t rsamp_ratio;

	/* check for the maximum rate the resampler supports */
	if (samp_rate > 3200000)
		samp_rate = 3200000;

	printf("Setting sample rate: %i Hz\n", samp_rate);
	rsamp_ratio = (CRYSTAL_FREQ * pow(2, 22)) / samp_rate;

	rsamp_ratio &= ~3;
	tmp = (rsamp_ratio >> 16);
	demod_write_reg(1, 0x9f, tmp, 2);
	tmp = rsamp_ratio & 0xffff;
	demod_write_reg(1, 0xa1, tmp, 2);
}

void set_i2c_repeater(int on)
{
	demod_write_reg(1, 0x01, on ? 0x18 : 0x10, 1);
}

void rtl_init(void)
{
	unsigned int i;

	/* default FIR coefficients used for DAB/FM by the Windows driver,
	 * the DVB driver uses different ones */
	uint8_t fir_coeff[] = {
		0xca, 0xdc, 0xd7, 0xd8, 0xe0, 0xf2, 0x0e, 0x35, 0x06, 0x50,
		0x9c, 0x0d, 0x71, 0x11, 0x14, 0x71, 0x74, 0x19, 0x41, 0x00,
	};

	/* initialize USB */
	rtl_write_reg(USBB, USB_SYSCTL, 0x09, 1);
	rtl_write_reg(USBB, USB_EPA_MAXPKT, 0x0002, 2);
	rtl_write_reg(USBB, USB_EPA_CTL, 0x1002, 2);

	/* poweron demod */
	rtl_write_reg(SYSB, DEMOD_CTL_1, 0x22, 1);
	rtl_write_reg(SYSB, DEMOD_CTL, 0xe8, 1);

	/* reset demod (bit 3, soft_rst) */
	demod_write_reg(1, 0x01, 0x14, 1);
	demod_write_reg(1, 0x01, 0x10, 1);

	/* disable spectrum inversion and adjacent channel rejection */
	demod_write_reg(1, 0x15, 0x00, 1);
	demod_write_reg(1, 0x16, 0x0000, 2);

	/* set IF-frequency to 0 Hz */
	demod_write_reg(1, 0x19, 0x0000, 2);

	/* set FIR coefficients */
	for (i = 0; i < sizeof (fir_coeff); i++)
		demod_write_reg(1, 0x1c + i, fir_coeff[i], 1);

	demod_write_reg(0, 0x19, 0x25, 1);

	/* init FSM state-holding register */
	demod_write_reg(1, 0x93, 0xf0, 1);

	/* disable AGC (en_dagc, bit 0) */
	demod_write_reg(1, 0x11, 0x00, 1);

	/* disable PID filter (enable_PID = 0) */
	demod_write_reg(0, 0x61, 0x60, 1);

	/* opt_adc_iq = 0, default ADC_I/ADC_Q datapath */
	demod_write_reg(0, 0x06, 0x80, 1);

	/* Enable Zero-IF mode (en_bbin bit), DC cancellation (en_dc_est),
	 * IQ estimation/compensation (en_iq_comp, en_iq_est) */
	demod_write_reg(1, 0xb1, 0x1b, 1);
}

void tuner_init(int frequency)
{
	set_i2c_repeater(1);

	switch (tuner_type) {
	case TUNER_E4000:
		e4000_Initialize(1);
		e4000_SetBandwidthHz(1, 8000000);
		e4000_SetRfFreqHz(1, frequency);
		break;
	case TUNER_FC0013:
		FC0013_Open();
		FC0013_SetFrequency(frequency/1000, 8);
		break;
	default:
		printf("No valid tuner available!");
		break;
	}

	printf("Tuned to %i Hz\n", frequency);
	set_i2c_repeater(0);
}

void usage(void)
{
	printf("rtl-sdr, an I/Q recorder for RTL2832 based USB-sticks\n\n"
		"Usage:\t -f frequency to tune to [Hz]\n"
		"\t[-s samplerate (default: 2048000 Hz)]\n"
		"\toutput filename\n");
	exit(1);
}

static void sighandler(int signum)
{
	do_exit = 1;
}

int main(int argc, char **argv)
{
	struct sigaction sigact;
	int r, opt;
	char *filename;
	uint32_t frequency = 0, samp_rate = 2048000;
	uint8_t buffer[READLEN];
	int n_read;
	FILE *file;

	while ((opt = getopt(argc, argv, "f:s:")) != -1) {
		switch (opt) {
		case 'f':
			frequency = atoi(optarg);
			break;
		case 's':
			samp_rate = atoi(optarg);
			break;
		default:
			usage();
			break;
		}
	}

	if (argc <= optind) {
		usage();
	} else {
		filename = argv[optind];
	}

	r = libusb_init(NULL);
	if (r < 0) {
		fprintf(stderr, "Failed to initialize libusb\n");
		exit(1);
	}

	r = find_device();
	if (r < 0) {
		fprintf(stderr, "Could not find/open device\n");
		goto out;
	}

	r = libusb_claim_interface(devh, 0);
	if (r < 0) {
		fprintf(stderr, "usb_claim_interface error %d\n", r);
		goto out;
	}

	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);

	/* Initialize the RTL2832 */
	rtl_init();
	set_samp_rate(samp_rate);

	/* Initialize tuner & set frequency */
	tuner_init(frequency);

	file = fopen(filename, "wb");

	if (!file) {
		printf("Failed to open %s\n", filename);
		goto out;
	}

	/* reset endpoint before we start reading */
	rtl_write_reg(USBB, USB_EPA_CTL, 0x1002, 2);
	rtl_write_reg(USBB, USB_EPA_CTL, 0x0000, 2);

	printf("Reading samples...\n");
	while (!do_exit) {
		libusb_bulk_transfer(devh, 0x81, buffer, READLEN, &n_read, 3000);
		fwrite(buffer, n_read, 1, file);

		if (n_read < READLEN) {
			printf("Short bulk read, samples lost, exiting!\n");
			break;
		}
	}

	fclose(file);
	libusb_release_interface(devh, 0);

out:
	libusb_close(devh);
	libusb_exit(NULL);
	return r >= 0 ? r : -r;
}

