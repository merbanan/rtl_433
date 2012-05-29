/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/*
 * written because people could not do real time
 * FM demod on Atom hardware with GNU radio
 * based on rtl_sdr.c
 * todo: realtime ARMv5
 *       remove float math (disqualifies complex.h)
 *       replace atan2 with a fast approximation
 *       in-place array operations
 *       wide band support
 *       refactor to look like rtl_tcp
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <Windows.h>
#endif

#include "rtl-sdr.h"

#define DEFAULT_SAMPLE_RATE		24000
#define DEFAULT_ASYNC_BUF_NUMBER	32
#define DEFAULT_BUF_LENGTH		1024
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)

static int do_exit = 0;
static rtlsdr_dev_t *dev = NULL;

struct fm_state
{
	int     now_r;
	int     now_j;
	int     pre_r;
	int     pre_j;
	int     prev_index;
	int     downsample;    /* min 4, max 256 */
	int     output_scale;
	int     squelch_level;
	int     signal[2048];  /* 16 bit signed i/q pairs */
	int16_t signal2[2048]; /* signal has lowpass, signal2 has demod */
	int     signal_len;
	FILE    *file;
};

void usage(void)
{
	#ifdef _WIN32
	fprintf(stderr,"rtl_fm, a simple FM demodulator for RTL2832 based USB-sticks\n\n"
		"Usage:\t rtl_fm-win.exe [device_index] [samplerate in kHz] "
		"[gain] [frequency in Hz] [filename]\n");
	#else
	fprintf(stderr,
		"rtl_fm, a simple narrow band FM demodulator for RTL2832 based DVB-T receivers\n\n"
		"Usage:\t -f frequency_to_tune_to [Hz]\n"
		"\t[-s samplerate (default: 24000 Hz)]\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-g tuner_gain (default: -1dB)]\n"
		"\t[-l squelch_level (default: 150)]\n"
		"\tfilename (a '-' dumps samples to stdout)\n\n"
		"Produces signed 16 bit ints, use sox to hear them.\n"
		"\trtl_fm ... | play -t raw -r 24k -e signed-integer -b 16 -c 1 -\n\n");
#endif
	exit(1);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		fprintf(stderr, "Signal caught, exiting!\n");
		do_exit = 1;
		rtlsdr_cancel_async(dev);
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	fprintf(stderr, "Signal caught, exiting!\n");
	do_exit = 1;
	rtlsdr_cancel_async(dev);
}
#endif

void rotate_90(unsigned char *buf, uint32_t len)
/* 90 rotation is 1+0j, 0+1j, -1+0j, 0-1j
   or [0, 1, -3, 2, -4, -5, 7, -6] */
{
	uint32_t i;
	unsigned char tmp;
	for (i=0; i<len; i+=8) {
		/* uint8_t negation = 255 - x */
		tmp = 255 - buf[i+3];
		buf[i+3] = buf[i+2];
		buf[i+2] = tmp;

		buf[i+4] = 255 - buf[i+4];
		buf[i+5] = 255 - buf[i+5];

		tmp = 255 - buf[i+6];
		buf[i+6] = buf[i+7];
		buf[i+7] = tmp;
	}
}

void low_pass(struct fm_state *fm, unsigned char *buf, uint32_t len)
{
	/* simple square window FIR */
	int i=0, i2=0;
	while (i < (int)len) {
		fm->now_r += ((int)buf[i]   - 128);
		fm->now_j += ((int)buf[i+1] - 128);
		i += 2;
		fm->prev_index++;
		if (fm->prev_index < (fm->downsample)) {
			continue;
		}
		fm->signal[i2]   = fm->now_r * fm->output_scale;
		fm->signal[i2+1] = fm->now_j * fm->output_scale;
		fm->prev_index = -1;
		fm->now_r = 0;
		fm->now_j = 0;
		i2 += 2;
	}
	fm->signal_len = i2;
}

/* define our own complex math ops
   because ARMv5 has no hardware float */

void multiply(int ar, int aj, int br, int bj, int *cr, int *cj)
{
	*cr = ar*br - aj*bj;
	*cj = aj*br + ar*bj;
}

int polar_discriminant(int ar, int aj, int br, int bj)
{
	int cr, cj;
	double angle;
	multiply(ar, aj, br, -bj, &cr, &cj);
	angle = atan2((double)cj, (double)cr);
	return (int)(angle / 3.14159 * (1<<14));
}

void fm_demod(struct fm_state *fm)
{
	int i, pcm;
	pcm = polar_discriminant(fm->signal[0], fm->signal[1],
		fm->pre_r, fm->pre_j);
	fm->signal2[0] = (int16_t)pcm;
	for (i = 2; i < (fm->signal_len); i += 2) {
		pcm = polar_discriminant(fm->signal[i], fm->signal[i+1],
			fm->signal[i-2], fm->signal[i-1]);
		fm->signal2[i/2] = (int16_t)pcm;
	}
	fm->pre_r = fm->signal[fm->signal_len - 2];
	fm->pre_j = fm->signal[fm->signal_len - 1];
}

int mad(int *samples, int len, int step)
/* mean average deviation */
{
	int i=0, sum=0, ave=0;
	for (i=0; i<len; i+=step) {
		sum += samples[i];
	}
	ave = sum / (len * step);
	sum = 0;
	for (i=0; i<len; i+=step) {
		sum += abs(samples[i] - ave);
	}
	return sum / (len * step);
}

void post_squelch(struct fm_state *fm)
{
	int i, i2, dev_r, dev_j, len, sq_l;
	/* only for small samples, big samples need chunk processing */
	len = fm->signal_len;
	sq_l = fm->squelch_level;
	dev_r = mad(&(fm->signal[0]), len, 2);
	dev_j = mad(&(fm->signal[1]), len, 2);
	if ((dev_r > sq_l) || (dev_j > sq_l)) {
		return;
	}
	/* weak signal, kill it entirely */
	for (i=0; i<len; i++) {
		fm->signal2[i/2] = 0;
	}
}

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	struct fm_state *fm2;
	if (!ctx) {
		return;
	}
	fm2 = (struct fm_struct*)(ctx);  // warning?
	rotate_90(buf, len);
	low_pass(fm2, buf, len);
	fm_demod(fm2);
	post_squelch(fm2);
	/* ignore under runs for now */
	fwrite(fm2->signal2, 2, fm2->signal_len/2, fm2->file);
}

static void optimal_settings(struct fm_state *fm, int freq, int rate)
{
	int r, capture_freq, capture_rate;
	fm->downsample = (1000000 / rate) + 1;
	fprintf(stderr, "Oversampling by: %ix.\n", fm->downsample);
	capture_rate = fm->downsample * rate;
	capture_freq = freq + capture_rate/4;
	fm->output_scale = (1<<15) / (128 * fm->downsample);
	if (fm->output_scale < 1) {
		fm->output_scale = 1;
	}
	fm->output_scale = 1;
	/* Set the sample rate */
	r = rtlsdr_set_sample_rate(dev, capture_rate);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set sample rate.\n");

	/* Set the frequency */
	r = rtlsdr_set_center_freq(dev, capture_freq);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set center freq.\n");
	else
		fprintf(stderr, "Tuned to %u Hz.\n", capture_freq);
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	struct fm_state fm; 
	char *filename = NULL;
	int n_read;
	int r, opt;
	int i, gain = -10; // tenths of a dB
	uint8_t *buffer;
	uint32_t dev_index = 0;
	uint32_t frequency = 100000000;
	uint32_t samp_rate = DEFAULT_SAMPLE_RATE;
	uint32_t out_block_size = DEFAULT_BUF_LENGTH;
	int device_count;
	char vendor[256], product[256], serial[256];
	fm.squelch_level = 150;
#ifndef _WIN32
	while ((opt = getopt(argc, argv, "d:f:g:s:b:l:S::")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = atoi(optarg);
			break;
		case 'f':
			frequency = (uint32_t)atof(optarg);
			break;
		case 'g':
			gain = (int)(atof(optarg) * 10);
			break;
		case 'l':
			fm.squelch_level = (int)atof(optarg);
			break;
		case 's':
			samp_rate = (uint32_t)atof(optarg);
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
#else
	if(argc <6)
		usage();
	dev_index = atoi(argv[1]);
	samp_rate = atoi(argv[2])*1000;
	gain=(int)(atof(argv[3]) * 10);
	frequency = atoi(argv[4]);
	filename = argv[5];
#endif
	out_block_size = DEFAULT_BUF_LENGTH;

	buffer = malloc(out_block_size * sizeof(uint8_t));

	device_count = rtlsdr_get_device_count();
	if (!device_count) {
		fprintf(stderr, "No supported devices found.\n");
		exit(1);
	}

	fprintf(stderr, "Found %d device(s):\n", device_count);
	for (i = 0; i < device_count; i++) {
		rtlsdr_get_device_usb_strings(i, vendor, product, serial);
		fprintf(stderr, "  %d:  %s, %s, SN: %s\n", i, vendor, product, serial);
	}
	fprintf(stderr, "\n");

	fprintf(stderr, "Using device %d: %s\n",
		dev_index, rtlsdr_get_device_name(dev_index));

	r = rtlsdr_open(&dev, dev_index);
	if (r < 0) {
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}
#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif

	optimal_settings(&fm, frequency, samp_rate);

	/* Set the tuner gain */
	r = rtlsdr_set_tuner_gain(dev, gain);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
	else
		fprintf(stderr, "Tuner gain set to %0.2f dB.\n", gain/10.0);

	if(strcmp(filename, "-") == 0) { /* Write samples to stdout */
		fm.file = stdout;
	} else {
		fm.file = fopen(filename, "wb");
		if (!fm.file) {
			fprintf(stderr, "Failed to open %s\n", filename);
			goto out;
		}
	}

	/* Reset endpoint before we start reading from it (mandatory) */
	r = rtlsdr_reset_buffer(dev);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to reset buffers.\n");

	fprintf(stderr, "Reading samples in async mode...\n");
	r = rtlsdr_read_async(dev, rtlsdr_callback, (void *)(&fm),
			      DEFAULT_ASYNC_BUF_NUMBER, out_block_size);

	if (do_exit)
		fprintf(stderr, "\nUser cancel, exiting...\n");
	else
		fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

	if (fm.file != stdout)
		fclose(fm.file);

	rtlsdr_close(dev);
	free (buffer);
out:
	return r >= 0 ? r : -r;
}
