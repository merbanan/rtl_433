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

#include "rtl-sdr.h"

#define READLEN		(16 * 16384)

static int do_exit = 0;

void usage(void)
{
	printf("rtl-sdr, an I/Q recorder for RTL2832 based DVB-T receivers\n\n"
		"Usage:\t -f frequency to tune to [Hz]\n"
		"\t[-s samplerate (default: 2048000 Hz)]\n"
	        "\t[-d device index (default: 0)]\n"
	        "\t[-g tuner gain (default: 0 dB)]\n"
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
	char *filename = NULL;
	uint32_t frequency = 0, samp_rate = 2048000;
	uint8_t buffer[READLEN];
	uint32_t n_read;
	FILE *file;
	rtlsdr_dev_t *dev = NULL;
	uint32_t dev_index = 0, gain = 0;

	while ((opt = getopt(argc, argv, "d:f:g:s:")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = atoi(optarg);
			break;
		case 'f':
			frequency = (int)atof(optarg);
			break;
		case 'g':
			gain = atoi(optarg);
			break;
		case 's':
			samp_rate = (int)atof(optarg);
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

	int device_count = rtlsdr_get_device_count();
	if (!device_count) {
		fprintf(stderr, "No supported devices found.\n");
		exit(1);
	}

	printf("Found %d device(s).\n", device_count);

	dev = rtlsdr_open(dev_index);
	if (NULL == dev) {
	fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}

	printf("Using %s\n", rtlsdr_get_device_name(dev_index));

	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);

	/* Set the sample rate */
	r = rtlsdr_set_sample_rate(dev, samp_rate);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set sample rate.\n");

	/* Set the frequency */
	r = rtlsdr_set_center_freq(dev, frequency);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set center freq.\n");
	else
		fprintf(stderr, "Tuned to %i Hz.\n", frequency);

	r = rtlsdr_set_tuner_gain(dev, gain);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
	else
		fprintf(stderr, "Tuner gain set to %i dB.\n", gain);

	file = fopen(filename, "wb");

	if (!file) {
		printf("Failed to open %s\n", filename);
		goto out;
	}

	/* Reset endpoint before we start reading from it (mandatory) */
	r = rtlsdr_reset_buffer(dev);
	if (r < 0)
		fprintf(stderr, "WARNING: Failed to reset buffers.\n");

	printf("Reading samples...\n");
	while (!do_exit) {
		r = rtlsdr_read_sync(dev, buffer, READLEN, &n_read);
		if (r < 0)
			fprintf(stderr, "WARNING: sync read failed.\n");

		fwrite(buffer, n_read, 1, file);

		if (n_read < READLEN) {
			printf("Short read, samples lost, exiting!\n");
			break;
		}
	}

	if (do_exit)
		printf("\nUser cancel, exiting...\n");

	fclose(file);

	rtlsdr_close(dev);
out:
	return r >= 0 ? r : -r;
}
