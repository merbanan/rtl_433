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

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef __APPLE__
#include <sys/time.h>
#else
#include <time.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#else
#include <Windows.h>
#include "getopt/getopt.h"
#endif

#include "rtl-sdr.h"

#define DEFAULT_SAMPLE_RATE		2048000
#define DEFAULT_ASYNC_BUF_NUMBER	32
#define DEFAULT_BUF_LENGTH		(16 * 16384)
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)

#define MHZ(x)	((x)*1000*1000)

#define PPM_DURATION			10

static int do_exit = 0;
static rtlsdr_dev_t *dev = NULL;

static int ppm_benchmark = 0;
static int64_t ppm_count = 0L;
static int64_t ppm_total = 0L;

#ifndef _WIN32
static struct timespec ppm_start;
static struct timespec ppm_recent;
static struct timespec ppm_now;
#endif

#ifdef __APPLE__
static struct timeval tv;
#endif

void usage(void)
{
	fprintf(stderr,
		"rtl_test, a benchmark tool for RTL2832 based DVB-T receivers\n\n"
		"Usage:\n"
		"\t[-s samplerate (default: 2048000 Hz)]\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-t enable Elonics E4000 tuner benchmark]\n"
		#ifndef _WIN32
		"\t[-p enable PPM error measurement]\n"
		#endif
		"\t[-b output_block_size (default: 16 * 16384)]\n"
		"\t[-S force sync output (default: async)]\n");
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

uint8_t bcnt, uninit = 1;

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	uint32_t i, lost = 0;
	int64_t ns;

	if (uninit) {
		bcnt = buf[0];
		uninit = 0;
	}

	for (i = 0; i < len; i++) {
		if(bcnt != buf[i]) {
			lost += (buf[i] > bcnt) ? (buf[i] - bcnt) : (bcnt - buf[i]);
			bcnt = buf[i];
		}

		bcnt++;
	}

	if (lost)
		printf("lost at least %d bytes\n", lost);

	if (!ppm_benchmark) {
		return;
	}
	ppm_count += (int64_t)len;
#ifndef _WIN32
	#ifndef __APPLE__
	clock_gettime(CLOCK_REALTIME, &ppm_now);
	#else
	gettimeofday(&tv, NULL);
	ppm_now.tv_sec = tv.tv_sec;
	ppm_now.tv_nsec = tv.tv_usec*1000;
	#endif
	if (ppm_now.tv_sec - ppm_recent.tv_sec > PPM_DURATION) {
		ns = 1000000000L * (int64_t)(ppm_now.tv_sec - ppm_recent.tv_sec);
		ns += (int64_t)(ppm_now.tv_nsec - ppm_recent.tv_nsec);
		printf("real sample rate: %i\n",
		(int)((1000000000L * ppm_count / 2L) / ns));
		#ifndef __APPLE__
		clock_gettime(CLOCK_REALTIME, &ppm_recent);
		#else
		gettimeofday(&tv, NULL);
		ppm_recent.tv_sec = tv.tv_sec;
		ppm_recent.tv_nsec = tv.tv_usec*1000;
		#endif
		ppm_total += ppm_count / 2L;
		ppm_count = 0L;
	}
#endif
}

void e4k_benchmark(void)
{
	uint32_t freq, gap_start = 0, gap_end = 0;
	uint32_t range_start = 0, range_end = 0;

	fprintf(stderr, "Benchmarking E4000 PLL...\n");

	/* find tuner range start */
	for (freq = MHZ(70); freq > MHZ(1); freq -= MHZ(1)) {
		if (rtlsdr_set_center_freq(dev, freq) < 0) {
			range_start = freq;
			break;
		}
	}

	/* find tuner range end */
	for (freq = MHZ(2000); freq < MHZ(2300UL); freq += MHZ(1)) {
		if (rtlsdr_set_center_freq(dev, freq) < 0) {
			range_end = freq;
			break;
		}
	}

	/* find start of L-band gap */
	for (freq = MHZ(1000); freq < MHZ(1300); freq += MHZ(1)) {
		if (rtlsdr_set_center_freq(dev, freq) < 0) {
			gap_start = freq;
			break;
		}
	}

	/* find end of L-band gap */
	for (freq = MHZ(1300); freq > MHZ(1000); freq -= MHZ(1)) {
		if (rtlsdr_set_center_freq(dev, freq) < 0) {
			gap_end = freq;
			break;
		}
	}

	fprintf(stderr, "E4K range: %i to %i MHz\n",
		range_start/MHZ(1) + 1, range_end/MHZ(1) - 1);

	fprintf(stderr, "E4K L-band gap: %i to %i MHz\n",
		gap_start/MHZ(1), gap_end/MHZ(1));
}

int main(int argc, char **argv)
{
#ifndef _WIN32
	struct sigaction sigact;
#endif
	int n_read;
	int r, opt;
	int i, tuner_benchmark = 0;
	int sync_mode = 0;
	uint8_t *buffer;
	uint32_t dev_index = 0;
	uint32_t samp_rate = DEFAULT_SAMPLE_RATE;
	uint32_t out_block_size = DEFAULT_BUF_LENGTH;
	int device_count;
	int count;
	int gains[100];
	int real_rate;
	int64_t ns;

	while ((opt = getopt(argc, argv, "d:s:b:tpS::")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = atoi(optarg);
			break;
		case 's':
			samp_rate = (uint32_t)atof(optarg);
			break;
		case 'b':
			out_block_size = (uint32_t)atof(optarg);
			break;
		case 't':
			tuner_benchmark = 1;
			break;
		case 'p':
			ppm_benchmark = PPM_DURATION;
			break;
		case 'S':
			sync_mode = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if(out_block_size < MINIMAL_BUF_LENGTH ||
	   out_block_size > MAXIMAL_BUF_LENGTH ){
		fprintf(stderr,
			"Output block size wrong value, falling back to default\n");
		fprintf(stderr,
			"Minimal length: %u\n", MINIMAL_BUF_LENGTH);
		fprintf(stderr,
			"Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
		out_block_size = DEFAULT_BUF_LENGTH;
	}

	buffer = malloc(out_block_size * sizeof(uint8_t));

	device_count = rtlsdr_get_device_count();
	if (!device_count) {
		fprintf(stderr, "No supported devices found.\n");
		exit(1);
	}

	fprintf(stderr, "Found %d device(s):\n", device_count);
	for (i = 0; i < device_count; i++)
		fprintf(stderr, "  %d:  %s\n", i, rtlsdr_get_device_name(i));
	fprintf(stderr, "\n");

	fprintf(stderr, "Using device %d: %s\n",
		dev_index,
		rtlsdr_get_device_name(dev_index));

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
	count = rtlsdr_get_tuner_gains(dev, NULL);
	fprintf(stderr, "Supported gain values (%d): ", count);

	count = rtlsdr_get_tuner_gains(dev, gains);
	for (i = 0; i < count; i++)
		fprintf(stderr, "%.1f ", gains[i] / 10.0);
	fprintf(stderr, "\n");

	/* Set the sample rate */
	r = rtlsdr_set_sample_rate(dev, samp_rate);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set sample rate.\n");

	if (tuner_benchmark) {
		if (rtlsdr_get_tuner_type(dev) == RTLSDR_TUNER_E4000)
			e4k_benchmark();
		else
			fprintf(stderr, "No E4000 tuner found, aborting.\n");

		goto exit;
	}

	/* Enable test mode */
	r = rtlsdr_set_testmode(dev, 1);

	/* Reset endpoint before we start reading from it (mandatory) */
	r = rtlsdr_reset_buffer(dev);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to reset buffers.\n");

	if (ppm_benchmark && !sync_mode) {
		fprintf(stderr, "Reporting PPM error measurement every %i seconds...\n", ppm_benchmark);
		fprintf(stderr, "Press ^C after a few minutes.\n");
#ifdef __APPLE__
		gettimeofday(&tv, NULL);
		ppm_recent.tv_sec = tv.tv_sec;
		ppm_recent.tv_nsec = tv.tv_usec*1000;
		ppm_start.tv_sec = tv.tv_sec;
		ppm_start.tv_nsec = tv.tv_usec*1000;
#elif __unix__
		clock_gettime(CLOCK_REALTIME, &ppm_recent);
		clock_gettime(CLOCK_REALTIME, &ppm_start);
#endif
	}

	if (sync_mode) {
		fprintf(stderr, "Reading samples in sync mode...\n");
		while (!do_exit) {
			r = rtlsdr_read_sync(dev, buffer, out_block_size, &n_read);
			if (r < 0) {
				fprintf(stderr, "WARNING: sync read failed.\n");
				break;
			}

			if ((uint32_t)n_read < out_block_size) {
				fprintf(stderr, "Short read, samples lost, exiting!\n");
				break;
			}
		}
	} else {
		fprintf(stderr, "Reading samples in async mode...\n");
		r = rtlsdr_read_async(dev, rtlsdr_callback, NULL,
				      DEFAULT_ASYNC_BUF_NUMBER, out_block_size);
	}

	if (do_exit) {
		fprintf(stderr, "\nUser cancel, exiting...\n");
		if (ppm_benchmark) {
#ifndef _WIN32
			ns = 1000000000L * (int64_t)(ppm_recent.tv_sec - ppm_start.tv_sec);
			ns += (int64_t)(ppm_recent.tv_nsec - ppm_start.tv_nsec);
			real_rate = (int)(ppm_total * 1000000000L / ns);
			printf("Cumulative PPM error: %i\n",
			(int)round((double)(1000000 * (real_rate - (int)samp_rate)) / (double)samp_rate));
#endif
		}
	}
	else
		fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

exit:
	rtlsdr_close(dev);
	free (buffer);

	return r >= 0 ? r : -r;
}
