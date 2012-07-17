/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012 by Hoernchen <la@tfc-server.de>
 * Copyright (C) 2012 by Kyle Keen <keenerd@gmail.com>
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
 * based on rtl_sdr.c and rtl_tcp.c
 * todo: realtime ARMv5
 *       remove float math (disqualifies complex.h)
 *       replace atan2 with a fast approximation
 *       in-place array operations
 *       wide band support
 *       sanity checks
 *       nicer FIR than square
 *       (tried this, was twice as slow and did not sound much better)
 *       scale squelch to other input parameters
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
#include "getopt/getopt.h"
#endif

#include <semaphore.h>
#include <pthread.h>
#include <libusb.h>

#include "rtl-sdr.h"

#define DEFAULT_SAMPLE_RATE		24000
#define DEFAULT_ASYNC_BUF_NUMBER	32
#define DEFAULT_BUF_LENGTH		(1 * 16384)
#define MINIMAL_BUF_LENGTH		512
#define MAXIMAL_BUF_LENGTH		(256 * 16384)
#define CONSEQ_SQUELCH                  4

static pthread_t demod_thread;
static sem_t data_ready;
static int do_exit = 0;
static rtlsdr_dev_t *dev = NULL;

struct fm_state
{
	int      now_r;
	int      now_j;
	int      pre_r;
	int      pre_j;
	int      prev_index;
	int      downsample;    /* min 1, max 256 */
	int      post_downsample;
	int      output_scale;
	int      squelch_level;
	int      squelch_hits;
	uint8_t  buf[DEFAULT_BUF_LENGTH];
	uint32_t buf_len;
	int      signal[DEFAULT_BUF_LENGTH];  /* 16 bit signed i/q pairs */
	int16_t  signal2[DEFAULT_BUF_LENGTH]; /* signal has lowpass, signal2 has demod */
	int      signal_len;
	FILE     *file;
	int      edge;
	uint32_t freqs[32];
	int      freq_len;
	int      freq_now;
	uint32_t sample_rate;
	int      fir_enable;
	int      fir[256];  /* fir_len == downsample */
	int      fir_sum;
	int      custom_atan;
};

void usage(void)
{
	fprintf(stderr,
		"rtl_fm, a simple narrow band FM demodulator for RTL2832 based DVB-T receivers\n\n"
		"Usage:\t -f frequency_to_tune_to [Hz]\n"
		"\t (use multiple -f for scanning)\n"
		"\t[-s samplerate (default: 24000 Hz)]\n"
		"\t[-d device_index (default: 0)]\n"
		"\t[-g tuner_gain (default: -1dB)]\n"
		"\t[-l squelch_level (default: 150)]\n"
		"\t[-E freq sets lower edge (default: center)]\n"
		"\tfilename (a '-' dumps samples to stdout)\n\n"
		"Experimental quality/cpu options:\n"
		"\t[-o oversampling (default: 1) !!BROKEN!!]\n"
		"\t[-F enables high quality FIR (default: off/square)]\n"
		"\t[-A enables high speed arctan (default: off)]\n\n"
		"Produces signed 16 bit ints, use Sox to hear them.\n"
		"\trtl_fm ... | play -t raw -r 24k -e signed-integer -b 16 -c 1 -V1 -\n\n");
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
/* simple square window FIR */
{
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

void build_fir(struct fm_state *fm)
/* for now, a simple triangle 
 * fancy FIRs are equally expensive, so use one */
/* point = sum(sample[i] * fir[i] * fir_len / fir_sum) */
{
	int i, len;
	len = fm->downsample;
	for(i = 0; i < len; i++) {
		fm->fir[i] = i;
	}
	for(i = len-1; i <= 0; i--) {
		fm->fir[i] = len - i;
	}
	fm->fir_sum = 0;
	for(i = 0; i < len; i++) {
		fm->fir_sum += fm->fir[i];
	}
}

void low_pass_fir(struct fm_state *fm, unsigned char *buf, uint32_t len)
/* perform an arbitrary FIR, doubles CPU use */
// possibly bugged, or overflowing
{
	int i=0, i2=0, i3=0;
	while (i < (int)len) {
		fm->prev_index++;
		i3 = fm->prev_index;
		fm->now_r += ((int)buf[i]   - 128) * fm->fir[i3] * fm->downsample / fm->fir_sum;
		fm->now_j += ((int)buf[i+1] - 128) * fm->fir[i3] * fm->downsample / fm->fir_sum;
		i += 2;
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

int low_pass_simple(int16_t *signal2, int len, int step)
// no wrap around, length must be multiple of step
{
	int i, i2, sum;
	for(i=0; i < len; i+=step) {
		sum = 0;
		for(i2=0; i2<step; i2++) {
			sum += (int)signal2[i + i2];
		}
		signal2[i] = (int16_t)(sum / step);
	}
	return len / step;
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
	//if (angle > (3.14159) || angle < (-3.14159))
	//	{fprintf(stderr, "overflow %f\n", angle);}
	return (int)(angle / 3.14159 * (1<<14));
}

int fast_atan2(int y, int x)
/* pre scaled for int16 */
{
	int yabs, angle;
	int pi4=(1<<12), pi34=3*(1<<12);  // note pi = 1<<14
	if (x==0 && y==0) {
		return 0;
	}
	yabs = y;
	if (yabs < 0) {
		yabs = -yabs;
	}
	if (x >= 0) {
		angle = pi4  - pi4 * (x-yabs) / (x+yabs);
	} else {
		angle = pi34 - pi4 * (x+yabs) / (yabs-x);
	}
	if (y < 0) {
		return -angle;
	}
	return angle;
}

int polar_disc_fast(int ar, int aj, int br, int bj)
{
	int cr, cj;
	multiply(ar, aj, br, -bj, &cr, &cj);
	return fast_atan2(cj, cr);
}

void fm_demod(struct fm_state *fm)
{
	int i, pcm;
	pcm = polar_discriminant(fm->signal[0], fm->signal[1],
		fm->pre_r, fm->pre_j);
	fm->signal2[0] = (int16_t)pcm;
	for (i = 2; i < (fm->signal_len); i += 2) {
		if (fm->custom_atan) {
			pcm = polar_disc_fast(fm->signal[i], fm->signal[i+1],
				fm->signal[i-2], fm->signal[i-1]);
		} else {
			pcm = polar_discriminant(fm->signal[i], fm->signal[i+1],
				fm->signal[i-2], fm->signal[i-1]);
		}
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

int post_squelch(struct fm_state *fm)
/* returns 1 for active signal, 0 for no signal */
{
	int i, i2, dev_r, dev_j, len, sq_l;
	/* only for small samples, big samples need chunk processing */
	len = fm->signal_len;
	sq_l = fm->squelch_level;
	dev_r = mad(&(fm->signal[0]), len, 2);
	dev_j = mad(&(fm->signal[1]), len, 2);
	if ((dev_r > sq_l) || (dev_j > sq_l)) {
		fm->squelch_hits = 0;
		return 1;
	}
	/* weak signal, kill it entirely */
	for (i=0; i<len; i++) {
		fm->signal2[i/2] = 0;
	}
	fm->squelch_hits++;
	return 0;
}

static void optimal_settings(struct fm_state *fm, int freq, int hopping)
{
	int r, capture_freq, capture_rate;
	fm->downsample = (1000000 / fm->sample_rate) + 1;
	fm->freq_now = freq;
	capture_rate = fm->downsample * fm->sample_rate;
	capture_freq = fm->freqs[freq] + capture_rate/4;
	capture_freq += fm->edge * fm->sample_rate / 2;
	fm->output_scale = (1<<15) / (128 * fm->downsample);
	if (fm->output_scale < 1) {
		fm->output_scale = 1;}
	fm->output_scale = 1;
	/* Set the frequency */
	r = rtlsdr_set_center_freq(dev, (uint32_t)capture_freq);
	if (hopping) {
		return;}
	fprintf(stderr, "Oversampling input by: %ix.\n", fm->downsample);
	fprintf(stderr, "Oversampling output by: %ix.\n", fm->post_downsample);
	fprintf(stderr, "Buffer size: %0.2fms\n",
		1000 * 0.5 * (float)DEFAULT_BUF_LENGTH / (float)capture_rate);
	if (r < 0) {
		fprintf(stderr, "WARNING: Failed to set center freq.\n");}
	else {
		fprintf(stderr, "Tuned to %u Hz.\n", capture_freq);}
    
	/* Set the sample rate */
	fprintf(stderr, "Sampling at %u Hz.\n", capture_rate);
	r = rtlsdr_set_sample_rate(dev, (uint32_t)capture_rate);
	if (r < 0) {
		fprintf(stderr, "WARNING: Failed to set sample rate.\n");}

}

void full_demod(unsigned char *buf, uint32_t len, struct fm_state *fm)
{
	int sr, freq_next;
	rotate_90(buf, len);
	if (fm->fir_enable) {
		low_pass_fir(fm, buf, len);
	} else {
		low_pass(fm, buf, len);
	}
	fm_demod(fm);
	sr = post_squelch(fm);
	if (fm->post_downsample > 1) {
		fm->signal_len = low_pass_simple(fm->signal2, fm->signal_len, fm->post_downsample);}
	/* ignore under runs for now */
	fwrite(fm->signal2, 2, fm->signal_len/2, fm->file);
	if (fm->freq_len > 1 && !sr && fm->squelch_hits > CONSEQ_SQUELCH) {
		freq_next = (fm->freq_now + 1) % fm->freq_len;
		optimal_settings(fm, freq_next, 1);
		fm->squelch_hits = CONSEQ_SQUELCH + 1;  /* hair trigger */
		/* wait for settling and dump buffer */
		usleep(5000);
		rtlsdr_read_sync(dev, NULL, 4096, NULL);
	}
}

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
	struct fm_state *fm2 = ctx;
	int dr_val;
	if (do_exit) {
		return;}
	if (!ctx) {
		return;}
	/* single threaded uses 25% less CPU? */
	/* full_demod(buf, len, fm2); */
	memcpy(fm2->buf, buf, len);
	fm2->buf_len = len;
	sem_getvalue(&data_ready, &dr_val);
	if (!dr_val) {
		sem_post(&data_ready);}
}

static void *demod_thread_fn(void *arg)
{
	struct fm_state *fm2 = arg;
	while (!do_exit) {
		sem_wait(&data_ready);
		full_demod(fm2->buf, fm2->buf_len, fm2);
	}
	return 0;
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
	int device_count;
	char vendor[256], product[256], serial[256];
	fm.freqs[0] = 100000000;
	fm.sample_rate = DEFAULT_SAMPLE_RATE;
	fm.squelch_level = 150;
	fm.freq_len = 0;
	fm.edge = 0;
	fm.fir_enable = 0;
	fm.prev_index = -1;
	fm.post_downsample = 1;
	fm.custom_atan = 0;
	sem_init(&data_ready, 0, 0);

	while ((opt = getopt(argc, argv, "d:f:g:s:b:l:o:EFA")) != -1) {
		switch (opt) {
		case 'd':
			dev_index = atoi(optarg);
			break;
		case 'f':
			fm.freqs[fm.freq_len] = (uint32_t)atof(optarg);
			fm.freq_len++;
			break;
		case 'g':
			gain = (int)(atof(optarg) * 10);
			break;
		case 'l':
			fm.squelch_level = (int)atof(optarg);
			break;
		case 's':
			fm.sample_rate = (uint32_t)atof(optarg);
			break;
		case 'o':
			fm.post_downsample = (int)atof(optarg);
			break;
		case 'E':
			fm.edge = 1;
			break;
		case 'F':
			fm.fir_enable = 1;
			break;
		case 'A':
			fm.custom_atan = 1;
			break;
		default:
			usage();
			break;
		}
	}
	/* double sample_rate to limit to Δθ to ±π */
	fm.sample_rate *= fm.post_downsample;

	if (argc <= optind) {
		usage();
	} else {
		filename = argv[optind];
	}

	buffer = malloc(DEFAULT_BUF_LENGTH * sizeof(uint8_t));

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

	optimal_settings(&fm, 0, 0);
	build_fir(&fm);

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

	pthread_create(&demod_thread, NULL, demod_thread_fn, (void *)(&fm));
	rtlsdr_read_async(dev, rtlsdr_callback, (void *)(&fm),
			      DEFAULT_ASYNC_BUF_NUMBER, DEFAULT_BUF_LENGTH);

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
