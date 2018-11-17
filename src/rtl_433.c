/*
 * rtl_433, turns your Realtek RTL2832 based DVB dongle into a 433.92MHz generic data receiver
 * Copyright (C) 2012 by Benjamin Larsson <benjamin@southpole.se>
 *
 * Based on rtl_sdr
 *
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

#include <stdbool.h>
#include <stddef.h>
#include <math.h>

#include "rtl_433.h"
#include "sdr.h"
#include "baseband.h"
#include "pulse_detect.h"
#include "pulse_demod.h"
#include "data.h"
#include "util.h"
#include "optparse.h"
#include "fileformat.h"
#include "am_analyze.h"

#define MAX_DATA_OUTPUTS 32
#define MAX_DUMP_OUTPUTS 8

#ifdef GIT_VERSION
#define STR_VALUE(arg) #arg
#define STR_EXPAND(s) STR_VALUE(s)
#define VERSION "version " STR_EXPAND(GIT_VERSION) " branch " STR_EXPAND(GIT_BRANCH) " at " STR_EXPAND(GIT_TIMESTAMP)
#else
#define VERSION "version unknown"
#endif

r_device *flex_create_device(char *spec); // maybe put this in some header file?

typedef enum {
    CONVERT_NATIVE,
    CONVERT_SI,
    CONVERT_CUSTOMARY
} conversion_mode_t;

struct app_cfg {
    int do_exit;
    int do_exit_async;
    int frequencies;
    uint32_t frequency[MAX_PROTOCOLS];
    uint32_t center_frequency;
    time_t rawtime_old;
    int duration;
    time_t stop_time;
    int stop_after_successful_events_flag;
    uint32_t samp_rate;
    uint64_t input_pos;
    uint32_t bytes_to_read;
    sdr_dev_t *dev;
    int include_only;  // Option -I
    int quiet_mode;
    int utc_mode;
    conversion_mode_t conversion_mode;
    int report_meta;
    uint16_t num_r_devices;
    void *output_handler[MAX_DATA_OUTPUTS];
    int last_output_handler;
    struct dm_state *demod;
};

static struct app_cfg cfg = {
    .do_exit = 0,
    .do_exit_async = 0,
    .frequencies = 0,
    .frequency = {0},
    .center_frequency = 0,
    .rawtime_old = 0,
    .duration = 0,
    .stop_time = 0,
    .stop_after_successful_events_flag = 0,
    .samp_rate = DEFAULT_SAMPLE_RATE,
    .input_pos = 0,
    .bytes_to_read = 0,
    .dev = NULL,
    .include_only = 0,
    .quiet_mode = 0,
    .utc_mode = 0,
    .conversion_mode = CONVERT_NATIVE,
    .report_meta = 0,
    .num_r_devices = 0,
    .output_handler = {0},
    .last_output_handler = 0,
    .demod = NULL,
};

float sample_file_pos = -1;
int debug_output = 0;

struct dm_state {
    int32_t level_limit;
    int16_t am_buf[MAXIMAL_BUF_LENGTH];  // AM demodulated signal (for OOK decoding)
    union {
        // These buffers aren't used at the same time, so let's use a union to save some memory
        int16_t fm[MAXIMAL_BUF_LENGTH];  // FM demodulated signal (for FSK decoding)
        uint16_t temp[MAXIMAL_BUF_LENGTH];  // Temporary buffer (to be optimized out..)
    } buf;
    uint8_t u8_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
    float f32_buf[MAXIMAL_BUF_LENGTH]; // format conversion buffer
    int sample_size; // CU8: 1, CS16: 2
    FilterState lowpass_filter_state;
    DemodFM_State demod_FM_state;
    int enable_FM_demod;
    am_analyze_t *am_analyze;
    int analyze_pulses;
    file_info_t load_info;
    file_info_t dumper[MAX_DUMP_OUTPUTS];
    int hop_time;

    /* Protocol states */
    uint16_t r_dev_num;
    struct protocol_state *r_devs[MAX_PROTOCOLS];

    pulse_data_t    pulse_data;
    pulse_data_t    fsk_pulse_data;
};

static void version(void)
{
    fprintf(stderr, "rtl_433 " VERSION "\n");
    exit(0);
}

static void usage(r_device *devices, int exit_code)
{
    int i;
    char disabledc;

    fprintf(stderr,
            "rtl_433, an ISM band generic data receiver for RTL2832 based DVB-T receivers\n"
            VERSION "\n"
            "\nUsage:\t= Tuner options =\n"
            "\t[-d <RTL-SDR USB device index> | :<RTL-SDR USB device serial> | <SoapySDR device query>]\n"
            "\t[-g <gain>] (default: auto)\n"
            "\t[-f <frequency>] [-f...] Receive frequency(s) (default: %i Hz)\n"
            "\t[-H <seconds>] Hop interval for polling of multiple frequencies (default: %i seconds)\n"
            "\t[-p <ppm_error] Correct rtl-sdr tuner frequency offset error (default: 0)\n"
            "\t[-s <sample rate>] Set sample rate (default: %i Hz)\n"
            "\t= Demodulator options =\n"
            "\t[-R <device>] Enable only the specified device decoding protocol (can be used multiple times)\n"
            "\t[-G] Enable all device protocols, included those disabled by default\n"
            "\t[-X <spec> | help] Add a general purpose decoder (-R 0 to disable all other decoders)\n"
            "\t[-l <level>] Change detection level used to determine pulses [0-16384] (0 = auto) (default: %i)\n"
            "\t[-z <value>] Override short value in data decoder\n"
            "\t[-x <value>] Override long value in data decoder\n"
            "\t[-n <value>] Specify number of samples to take (each sample is 2 bytes: 1 each of I & Q)\n"
            "\t= Analyze/Debug options =\n"
            "\t[-a] Analyze mode. Print a textual description of the signal.\n"
            "\t[-A] Pulse Analyzer. Enable pulse analysis and decode attempt.\n"
            "\t\t Disable all decoders with -R 0 if you want analyzer output only.\n"
            "\t[-I] Include only: 0 = all (default), 1 = unknown devices, 2 = known devices\n"
            "\t[-D] Print debug info on event (repeat for more info)\n"
            "\t[-q] Quiet mode, suppress non-data messages\n"
            "\t[-y <code>] Verify decoding of demodulated test data (e.g. \"{25}fb2dd58\") with enabled devices\n"
            "\t= File I/O options =\n"
            "\t[-t] Test signal auto save. Use it together with analyze mode (-a -t). Creates one file per signal\n"
            "\t\t Note: Saves raw I/Q samples (uint8 pcm, 2 channel). Preferred mode for generating test files\n"
            "\t[-r <filename>] Read data from input file instead of a receiver\n"
            "\t[-w <filename>] Save data stream to output file (a '-' dumps samples to stdout)\n"
            "\t[-W <filename>] Save data stream to output file, overwrite existing file\n"
            "\t[-F] kv|json|csv|syslog|null Produce decoded output in given format. Not yet supported by all drivers.\n"
            "\t\t Append output to file with :<filename> (e.g. -F csv:log.csv), defaults to stdout.\n"
            "\t\t Specify host/port for syslog with e.g. -F syslog:127.0.0.1:1514\n"
            "\t[-C] native|si|customary Convert units in decoded output.\n"
            "\t[-T] Specify number of seconds to run\n"
            "\t[-U] Print timestamps in UTC (this may also be accomplished by invocation with TZ environment variable set).\n"
            "\t[-E] Stop after outputting successful event(s)\n"
            "\t[-V] Output the version string and exit\n"
            "\t[-h] Output this usage help and exit\n"
            "\t\t Use -d, -g, -R, -X, -F, -r, or -w without argument for more help\n\n",
            DEFAULT_FREQUENCY, DEFAULT_HOP_TIME, DEFAULT_SAMPLE_RATE, DEFAULT_LEVEL_LIMIT);

    if (devices) {
        fprintf(stderr, "Supported device protocols:\n");
        for (i = 0; i < cfg.num_r_devices; i++) {
            disabledc = devices[i].disabled ? '*' : ' ';
            if (devices[i].disabled <= 1) // if not hidden
                fprintf(stderr, "    [%02d]%c %s\n", i + 1, disabledc, devices[i].name);
        }
        fprintf(stderr, "\n* Disabled by default, use -R n or -G\n");
    }
    exit(exit_code);
}

static void help_device(void)
{
    fprintf(stderr,
#ifdef RTLSDR
            "\tRTL-SDR device driver is available.\n"
#else
            "\tRTL-SDR device driver is not available.\n"
#endif
            "[-d <RTL-SDR USB device index>] (default: 0)\n"
            "[-d :<RTL-SDR USB device serial (can be set with rtl_eeprom -s)>]\n"
            "\tTo set gain for RTL-SDR use -g X to set an overall gain in tenths of dB.\n"
#ifdef SOAPYSDR
            "\tSoapySDR device driver is available.\n"
#else
            "\tSoapySDR device driver is not available.\n"
#endif
            "[-d \"\" Open default SoapySDR device\n"
            "[-d driver=rtlsdr Open e.g. specific SoapySDR device\n"
            "\tTo set gain for SoapySDR use -g ELEM=val,ELEM=val,... e.g. -g LNA=20,TIA=8,PGA=2 (for LimeSDR).\n");
    exit(0);
}

static void help_gain(void)
{
    fprintf(stderr,
            "-g <gain>] (default: auto)\n"
            "\tFor RTL-SDR: gain in tenths of dB (\"0\" is auto).\n"
            "\tFor SoapySDR: gain in dB for automatic distribution (\"\" is auto), or string of gain elements.\n"
            "\tE.g. \"LNA=20,TIA=8,PGA=2\" for LimeSDR.\n");
    exit(0);
}

static void help_output(void)
{
    fprintf(stderr,
            "[-F] kv|json|csv|syslog|null Produce decoded output in given format. Not yet supported by all drivers.\n"
            "\tWithout this option the default is KV output. Use \"-F null\" to remove the default.\n"
            "\tAppend output to file with :<filename> (e.g. -F csv:log.csv), defaults to stdout.\n"
            "\tSpecify host/port for syslog with e.g. -F syslog:127.0.0.1:1514\n");
    exit(0);
}

static void help_read(void)
{
    fprintf(stderr,
            "[-r <filename>] Read data from input file instead of a receiver\n"
            "\tParameters are detected from the full path, file name, and extension.\n\n"
            "\tA center frequency is detected as (fractional) number suffixed with 'M',\n"
            "\t'Hz', 'kHz', 'MHz', or 'GHz'.\n\n"
            "\tA sample rate is detected as (fractional) number suffixed with 'k',\n"
            "\t'sps', 'ksps', 'Msps', or 'Gsps'.\n\n"
            "\tFile content and format are detected as parameters, possible options are:\n"
            "\t'cu8', 'cs16', 'cf32' ('IQ' implied), and 'am.s16'.\n\n"
            "\tParameters must be separated by non-alphanumeric chars and are case-insensitive.\n"
            "\tOverrides can be prefixed, separated by colon (':')\n\n"
            "\tE.g. default detection by extension: path/filename.am.s16\n"
            "\tforced overrides: am:s16:path/filename.ext\n");
    exit(0);
}

static void help_write(void)
{
    fprintf(stderr,
            "[-w <filename>] Save data stream to output file (a '-' dumps samples to stdout)\n"
            "[-W <filename>] Save data stream to output file, overwrite existing file\n"
            "\tParameters are detected from the full path, file name, and extension.\n\n"
            "\tFile content and format are detected as parameters, possible options are:\n"
            "\t'cu8', 'cs16', 'cf32' ('IQ' implied),\n"
            "\t'am.s16', 'am.f32', 'fm.s16', 'fm.f32',\n"
            "\t'i.f32', 'q.f32', 'logic.u8', and 'vcd'.\n\n"
            "\tParameters must be separated by non-alphanumeric chars and are case-insensitive.\n"
            "\tOverrides can be prefixed, separated by colon (':')\n\n"
            "\tE.g. default detection by extension: path/filename.am.s16\n"
            "\tforced overrides: am:s16:path/filename.ext\n");
    exit(0);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum) {
    if (CTRL_C_EVENT == signum) {
        fprintf(stderr, "Signal caught, exiting!\n");
        cfg.do_exit = 1;
        sdr_stop(cfg.dev);
        return TRUE;
    }
    return FALSE;
}
#else
static void sighandler(int signum) {
    if (signum == SIGPIPE) {
        signal(SIGPIPE,SIG_IGN);
    } else if (signum == SIGALRM) {
        fprintf(stderr, "Async read stalled, exiting!\n");
    } else {
        fprintf(stderr, "Signal caught, exiting!\n");
    }
    cfg.do_exit = 1;
    sdr_stop(cfg.dev);
}
#endif


static void register_protocol(struct dm_state *demod, r_device *t_dev) {
    struct protocol_state *p = calloc(1, sizeof (struct protocol_state));
    p->short_limit = (float) t_dev->short_limit / (1000000.0 / (float)cfg.samp_rate);
    p->long_limit = (float) t_dev->long_limit / (1000000.0 / (float)cfg.samp_rate);
    p->reset_limit = (float) t_dev->reset_limit / (1000000.0 / (float)cfg.samp_rate);
    p->gap_limit = (float) t_dev->gap_limit / ( 1000000.0 / (float) cfg.samp_rate);
    p->sync_width = (float) t_dev->sync_width / (1000000.0 / (float)cfg.samp_rate);
    p->tolerance = (float) t_dev->tolerance / (1000000.0 / (float)cfg.samp_rate);
    p->modulation = t_dev->modulation;
    p->callback = t_dev->json_callback;
    p->name = t_dev->name;
    p->demod_arg = t_dev->demod_arg;

    demod->r_devs[demod->r_dev_num] = p;
    demod->r_dev_num++;

    if (!cfg.quiet_mode) {
    fprintf(stderr, "Registering protocol [%d] \"%s\"\n", t_dev->protocol_num, t_dev->name);
    }

    if (demod->r_dev_num > MAX_PROTOCOLS) {
        fprintf(stderr, "\n\nMax number of protocols reached %d\n", MAX_PROTOCOLS);
    fprintf(stderr, "Increase MAX_PROTOCOLS and recompile\n");
    exit(-1);
    }
}

static void calc_rssi_snr(pulse_data_t *pulse_data)
{
    float asnr   = (float)pulse_data->ook_high_estimate / ((float)pulse_data->ook_low_estimate + 1);
    float foffs1 = (float)pulse_data->fsk_f1_est / INT16_MAX * cfg.samp_rate / 2.0;
    float foffs2 = (float)pulse_data->fsk_f2_est / INT16_MAX * cfg.samp_rate / 2.0;
    pulse_data->freq1_hz = (foffs1 + cfg.center_frequency);
    pulse_data->freq2_hz = (foffs2 + cfg.center_frequency);
    // NOTE: for (CU8) amplitude is 10x (because it's squares)
    if (cfg.demod->sample_size == 1) { // amplitude (CU8)
        pulse_data->rssi_db = 10.0f * log10f(pulse_data->ook_high_estimate) - 42.1442f; // 10*log10f(16384.0f)
        pulse_data->noise_db = 10.0f * log10f(pulse_data->ook_low_estimate) - 42.1442f; // 10*log10f(16384.0f)
        pulse_data->snr_db  = 10.0f * log10f(asnr);
    } else { // magnitude (CS16)
        pulse_data->rssi_db = 20.0f * log10f(pulse_data->ook_high_estimate) - 84.2884f; // 20*log10f(16384.0f)
        pulse_data->noise_db = 20.0f * log10f(pulse_data->ook_low_estimate) - 84.2884f; // 20*log10f(16384.0f)
        pulse_data->snr_db  = 20.0f * log10f(asnr);
    }
}

/* handles incoming structured data by dumping it */
void data_acquired_handler(data_t *data)
{
    if (cfg.conversion_mode == CONVERT_SI) {
        for (data_t *d = data; d; d = d->next) {
            // Convert double type fields ending in _F to _C
            if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_F")) {
                *(double*)d->value = fahrenheit2celsius(*(double*)d->value);
                char *new_label = str_replace(d->key, "_F", "_C");
                free(d->key);
                d->key = new_label;
                char *pos;
                if (d->format && (pos = strrchr(d->format, 'F'))) {
                    *pos = 'C';
                }
            }
            // Convert double type fields ending in _mph to _kph
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mph")) {
                *(double*)d->value = mph2kmph(*(double*)d->value);
                char *new_label = str_replace(d->key, "_mph", "_kph");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "mph", "kph");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _mph to _kph
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_inch")) {
                *(double*)d->value = inch2mm(*(double*)d->value);
                char *new_label = str_replace(d->key, "_inch", "_mm");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "inch", "mm");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _inHg to _hPa
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_inHg")) {
                *(double*)d->value = inhg2hpa(*(double*)d->value);
                char *new_label = str_replace(d->key, "_inHg", "_hPa");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "inHg", "hPa");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _PSI to _kPa
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_PSI")) {
                *(double*)d->value = psi2kpa(*(double*)d->value);
                char *new_label = str_replace(d->key, "_PSI", "_kPa");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "PSI", "kPa");
                free(d->format);
                d->format = new_format_label;
            }
        }
    }
    if (cfg.conversion_mode == CONVERT_CUSTOMARY) {
        for (data_t *d = data; d; d = d->next) {
            // Convert double type fields ending in _C to _F
            if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_C")) {
                *(double*)d->value = celsius2fahrenheit(*(double*)d->value);
                char *new_label = str_replace(d->key, "_C", "_F");
                free(d->key);
                d->key = new_label;
                char *pos;
                if (d->format && (pos = strrchr(d->format, 'C'))) {
                    *pos = 'F';
                }
            }
            // Convert double type fields ending in _kph to _mph
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_kph")) {
                *(double*)d->value = kmph2mph(*(double*)d->value);
                char *new_label = str_replace(d->key, "_kph", "_mph");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "kph", "mph");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _mm to _inch
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mm")) {
                *(double*)d->value = mm2inch(*(double*)d->value);
                char *new_label = str_replace(d->key, "_mm", "_inch");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "mm", "inch");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _hPa to _inHg
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_hPa")) {
                *(double*)d->value = hpa2inhg(*(double*)d->value);
                char *new_label = str_replace(d->key, "_hPa", "_inHg");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "hPa", "inHg");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _kPa to _PSI
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_kPa")) {
                *(double*)d->value = kpa2psi(*(double*)d->value);
                char *new_label = str_replace(d->key, "_kPa", "_PSI");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "kPa", "PSI");
                free(d->format);
                d->format = new_format_label;
            }
        }
    }

    if (cfg.report_meta && cfg.demod->fsk_pulse_data.fsk_f2_est) {
        calc_rssi_snr(&cfg.demod->fsk_pulse_data);
        data_append(data,
                "mod",   "Modulation",  DATA_STRING, "FSK",
                "freq1", "Freq1",       DATA_FORMAT, "%.1f MHz", DATA_DOUBLE, cfg.demod->fsk_pulse_data.freq1_hz / 1000000.0,
                "freq2", "Freq2",       DATA_FORMAT, "%.1f MHz", DATA_DOUBLE, cfg.demod->fsk_pulse_data.freq2_hz / 1000000.0,
                "rssi",  "RSSI",        DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg.demod->fsk_pulse_data.rssi_db,
                "snr",   "SNR",         DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg.demod->fsk_pulse_data.snr_db,
                "noise", "Noise",       DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg.demod->fsk_pulse_data.noise_db,
                NULL);
    }
    else if (cfg.report_meta) {
        calc_rssi_snr(&cfg.demod->pulse_data);
        data_append(data,
                "mod",   "Modulation",  DATA_STRING, "ASK",
                "freq",  "Freq",        DATA_FORMAT, "%.1f MHz", DATA_DOUBLE, cfg.demod->pulse_data.freq1_hz / 1000000.0,
                "rssi",  "RSSI",        DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg.demod->pulse_data.rssi_db,
                "snr",   "SNR",         DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg.demod->pulse_data.snr_db,
                "noise", "Noise",       DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg.demod->pulse_data.noise_db,
                NULL);
    }

    for (int i = 0; i < cfg.last_output_handler; ++i) {
        data_output_print(cfg.output_handler[i], data);
    }
    data_free(data);
}

static void sdr_callback(unsigned char *iq_buf, uint32_t len, void *ctx) {
    struct dm_state *demod = ctx;
    int i;
    char time_str[LOCAL_TIME_BUFLEN];
    unsigned long n_samples;

    if (cfg.do_exit || cfg.do_exit_async)
        return;

    if ((cfg.bytes_to_read > 0) && (cfg.bytes_to_read <= len)) {
        len = cfg.bytes_to_read;
        cfg.do_exit = 1;
        sdr_stop(cfg.dev);
    }

    n_samples = len / 2 / demod->sample_size;

#ifndef _WIN32
    alarm(3); // require callback to run every 3 second, abort otherwise
#endif

    if (demod->am_analyze) {
        am_analyze_add(demod->am_analyze, iq_buf, len);
    }

    // AM demodulation
    if (demod->sample_size == 1) { // CU8
        envelope_detect(iq_buf, demod->buf.temp, n_samples);
        //magnitude_true_cu8(iq_buf, demod->buf.temp, n_samples);
        //magnitude_est_cu8(iq_buf, demod->buf.temp, n_samples);
    } else { // CS16
        //magnitude_true_cs16((int16_t *)iq_buf, demod->buf.temp, n_samples);
        magnitude_est_cs16((int16_t *)iq_buf, demod->buf.temp, n_samples);
    }
    baseband_low_pass_filter(demod->buf.temp, demod->am_buf, n_samples, &demod->lowpass_filter_state);

    // FM demodulation
    if (demod->enable_FM_demod) {
        if (demod->sample_size == 1) { // CU8
            baseband_demod_FM(iq_buf, demod->buf.fm, n_samples, &demod->demod_FM_state);
        } else { // CS16
            baseband_demod_FM_cs16((int16_t *)iq_buf, demod->buf.fm, n_samples, &demod->demod_FM_state);
        }
    }

    // Handle special input formats
    if (demod->load_info.format == S16_AM) { // The IQ buffer is really AM demodulated data
        memcpy(demod->am_buf, iq_buf, len);
    } else if (demod->load_info.format == S16_FM) { // The IQ buffer is really FM demodulated data
        // we would need AM for the envelope too
        memcpy(demod->buf.fm, iq_buf, len);
    }

    int d_events = 0; // Sensor events successfully detected
    if (demod->r_dev_num || demod->analyze_pulses || (demod->dumper->spec)) {
        // Detect a package and loop through demodulators with pulse data
        int package_type = 1;  // Just to get us started
        for (file_info_t const *dumper = demod->dumper; dumper->spec && *dumper->spec; ++dumper) {
            if (dumper->format == U8_LOGIC) {
                memset(demod->u8_buf, 0, n_samples);
                break;
            }
        }
        while (package_type) {
            int p_events = 0; // Sensor events successfully detected per package
            package_type = pulse_detect_package(demod->am_buf, demod->buf.fm, n_samples, demod->level_limit, cfg.samp_rate, cfg.input_pos, &demod->pulse_data, &demod->fsk_pulse_data);
            if (package_type == 1) {
                if (demod->analyze_pulses) fprintf(stderr, "Detected OOK package\t@ %s\n", local_time_str(0, time_str));
                for (i = 0; i < demod->r_dev_num; i++) {
                    switch (demod->r_devs[i]->modulation) {
                        case OOK_PULSE_PCM_RZ:
                            p_events += pulse_demod_pcm(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        case OOK_PULSE_PPM_RAW:
                            p_events += pulse_demod_ppm(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        case OOK_PULSE_PWM_PRECISE:
                            p_events += pulse_demod_pwm_precise(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        case OOK_PULSE_PWM_RAW:
                            p_events += pulse_demod_pwm(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        case OOK_PULSE_MANCHESTER_ZEROBIT:
                            p_events += pulse_demod_manchester_zerobit(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        case OOK_PULSE_PIWM_RAW:
                            p_events += pulse_demod_piwm_raw(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        case OOK_PULSE_PIWM_DC:
                            p_events += pulse_demod_piwm_dc(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        case OOK_PULSE_DMC:
                            p_events += pulse_demod_dmc(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        case OOK_PULSE_PWM_OSV1:
                            p_events += pulse_demod_osv1(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        // FSK decoders
                        case FSK_PULSE_PCM:
                        case FSK_PULSE_PWM_RAW:
                            break;
                        case FSK_PULSE_MANCHESTER_ZEROBIT:
                            p_events += pulse_demod_manchester_zerobit(&demod->pulse_data, demod->r_devs[i]);
                            break;
                        default:
                            fprintf(stderr, "Unknown modulation %d in protocol!\n", demod->r_devs[i]->modulation);
                    }
                } // for demodulators
                for (file_info_t const *dumper = demod->dumper; dumper->spec && *dumper->spec; ++dumper) {
                    if (dumper->format == VCD_LOGIC) pulse_data_print_vcd(dumper->file, &demod->pulse_data, '\'', cfg.samp_rate);
                    if (dumper->format == U8_LOGIC) pulse_data_dump_raw(demod->u8_buf, n_samples, cfg.input_pos, &demod->pulse_data, 0x02);
                }
                if (debug_output > 1) pulse_data_print(&demod->pulse_data);
                if (demod->analyze_pulses && (cfg.include_only == 0 || (cfg.include_only == 1 && p_events == 0) || (cfg.include_only == 2 && p_events > 0)) ) {
                    calc_rssi_snr(&demod->pulse_data);
                    pulse_analyzer(&demod->pulse_data, cfg.samp_rate);
                }
            } else if (package_type == 2) {
                if (demod->analyze_pulses) fprintf(stderr, "Detected FSK package\t@ %s\n", local_time_str(0, time_str));
                for (i = 0; i < demod->r_dev_num; i++) {
                    switch (demod->r_devs[i]->modulation) {
                        // OOK decoders
                        case OOK_PULSE_PCM_RZ:
                        case OOK_PULSE_PPM_RAW:
                        case OOK_PULSE_PWM_PRECISE:
                        case OOK_PULSE_PWM_RAW:
                        case OOK_PULSE_MANCHESTER_ZEROBIT:
                        case OOK_PULSE_PIWM_RAW:
                        case OOK_PULSE_PIWM_DC:
                        case OOK_PULSE_DMC:
                        case OOK_PULSE_PWM_OSV1:
                            break;
                        case FSK_PULSE_PCM:
                            p_events += pulse_demod_pcm(&demod->fsk_pulse_data, demod->r_devs[i]);
                            break;
                        case FSK_PULSE_PWM_RAW:
                            p_events += pulse_demod_pwm(&demod->fsk_pulse_data, demod->r_devs[i]);
                            break;
                        case FSK_PULSE_MANCHESTER_ZEROBIT:
                            p_events += pulse_demod_manchester_zerobit(&demod->fsk_pulse_data, demod->r_devs[i]);
                            break;
                        default:
                            fprintf(stderr, "Unknown modulation %d in protocol!\n", demod->r_devs[i]->modulation);
                    }
                } // for demodulators
                for (file_info_t const *dumper = demod->dumper; dumper->spec && *dumper->spec; ++dumper) {
                    if (dumper->format == VCD_LOGIC) pulse_data_print_vcd(dumper->file, &demod->fsk_pulse_data, '"', cfg.samp_rate);
                    if (dumper->format == U8_LOGIC) pulse_data_dump_raw(demod->u8_buf, n_samples, cfg.input_pos, &demod->fsk_pulse_data, 0x04);
                }
                if (debug_output > 1) pulse_data_print(&demod->fsk_pulse_data);
                if (demod->analyze_pulses && (cfg.include_only == 0 || (cfg.include_only == 1 && p_events == 0) || (cfg.include_only == 2 && p_events > 0)) ) {
                    calc_rssi_snr(&demod->fsk_pulse_data);
                    pulse_analyzer(&demod->fsk_pulse_data, cfg.samp_rate);
                }
            } // if (package_type == ...
            d_events += p_events;
        } // while (package_type)...

        // dump partial pulse_data for this buffer
        for (file_info_t const *dumper = demod->dumper; dumper->spec && *dumper->spec; ++dumper) {
            if (dumper->format == U8_LOGIC) {
                pulse_data_dump_raw(demod->u8_buf, n_samples, cfg.input_pos, &demod->pulse_data, 0x02);
                pulse_data_dump_raw(demod->u8_buf, n_samples, cfg.input_pos, &demod->fsk_pulse_data, 0x04);
                break;
            }
        }

        if (cfg.stop_after_successful_events_flag && (d_events > 0)) {
            cfg.do_exit = cfg.do_exit_async = 1;
            sdr_stop(cfg.dev);
        }
    }

    if (demod->am_analyze) {
        if (cfg.include_only == 0 || (cfg.include_only == 1 && d_events == 0) || (cfg.include_only == 2 && d_events > 0)) {
            am_analyze(demod->am_analyze, demod->am_buf, n_samples, debug_output);
        } else {
            am_analyze_reset(demod->am_analyze);
        }
    }

    for (file_info_t const *dumper = demod->dumper; dumper->spec && *dumper->spec; ++dumper) {
        if (!dumper->file || dumper->format == VCD_LOGIC)
            continue;
        uint8_t *out_buf = iq_buf;  // Default is to dump IQ samples
        unsigned long out_len = n_samples * 2 * demod->sample_size;

        if (dumper->format == CU8_IQ) {
            if (demod->sample_size == 2) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((uint8_t *)demod->buf.temp)[n] = (((int16_t *)iq_buf)[n] >> 8) + 128; // scale Q0.15 to Q0.7
                out_buf = (uint8_t *)demod->buf.temp;
                out_len = n_samples * 2 * sizeof(uint8_t);
            }

        } else if (dumper->format == CS16_IQ) {
            if (demod->sample_size == 1) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((int16_t *)demod->buf.temp)[n] = (iq_buf[n] - 128) << 8; // scale Q0.7 to Q0.15
                out_buf = (uint8_t *)demod->buf.temp; // this buffer is too small if out_block_size is large
                out_len = n_samples * 2 * sizeof(int16_t);
            }

        } else if (dumper->format == S16_AM) {
            out_buf = (uint8_t *)demod->am_buf;
            out_len = n_samples * sizeof(int16_t);

        } else if (dumper->format == S16_FM) {
            out_buf = (uint8_t *)demod->buf.fm;
            out_len = n_samples * sizeof(int16_t);

        } else if (dumper->format == F32_AM) {
            for (unsigned long n = 0; n < n_samples; ++n)
                demod->f32_buf[n] = demod->am_buf[n] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);

        } else if (dumper->format == F32_FM) {
            for (unsigned long n = 0; n < n_samples; ++n)
                demod->f32_buf[n] = demod->buf.fm[n] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);

        } else if (dumper->format == F32_I) {
            if (demod->sample_size == 1)
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = (iq_buf[n * 2] - 128) * (1.0 / 0x80); // scale from Q0.7
            else
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = ((int16_t *)iq_buf)[n * 2] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);

        } else if (dumper->format == F32_Q) {
            if (demod->sample_size == 1)
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = (iq_buf[n * 2 + 1] - 128) * (1.0 / 0x80); // scale from Q0.7
            else
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = ((int16_t *)iq_buf)[n * 2 + 1] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);

        } else if (dumper->format == U8_LOGIC) { // state data
            out_buf = demod->u8_buf;
            out_len = n_samples;
        }

        if (fwrite(out_buf, 1, out_len, dumper->file) != out_len) {
            fprintf(stderr, "Short write, samples lost, exiting!\n");
            sdr_stop(cfg.dev);
        }
    }

    cfg.input_pos += n_samples;
    if (cfg.bytes_to_read > 0)
        cfg.bytes_to_read -= len;

    time_t rawtime;
    time(&rawtime);
	if (cfg.frequencies > 1 && difftime(rawtime, cfg.rawtime_old) > demod->hop_time) {
	  cfg.rawtime_old = rawtime;
	  cfg.do_exit_async = 1;
#ifndef _WIN32
	  alarm(0); // cancel the watchdog timer
#endif
	  sdr_stop(cfg.dev);
	}
    if (cfg.duration > 0 && rawtime >= cfg.stop_time) {
        cfg.do_exit_async = cfg.do_exit = 1;
#ifndef _WIN32
        alarm(0); // cancel the watchdog timer
#endif
        sdr_stop(cfg.dev);
        fprintf(stderr, "Time expired, exiting!\n");
    }
}

// find the fields output for CSV
static char const **determine_csv_fields(r_device *devices, int num_devices, r_device *extra_device, int *num_fields)
{
    int i;
    int cur_output_fields = 0;
    int num_output_fields = 0;
    const char **output_fields = NULL;
    for (i = 0; i < num_devices; i++) {
        if (!devices[i].disabled) {
            if (devices[i].fields)
                for (int c = 0; devices[i].fields[c]; ++c)
                    ++num_output_fields;
            else
                fprintf(stderr, "rtl_433: warning: %d \"%s\" does not support CSV output\n",
                        i, devices[i].name);
        }
    }
    if (extra_device && !extra_device->disabled) {
        if (extra_device->fields)
            for (int c = 0; extra_device->fields[c]; ++c)
                ++num_output_fields;
        else
            fprintf(stderr, "rtl_433: warning: %d \"%s\" does not support CSV output\n",
                    i, extra_device->name);
    }
    output_fields = calloc(num_output_fields + 1, sizeof(char *));
    for (i = 0; i < num_devices; i++) {
        if (!devices[i].disabled && devices[i].fields) {
            for (int c = 0; devices[i].fields[c]; ++c) {
                output_fields[cur_output_fields] = devices[i].fields[c];
                ++cur_output_fields;
            }
        }
    }
    if (extra_device && !extra_device->disabled && extra_device->fields) {
        for (int c = 0; extra_device->fields[c]; ++c) {
            output_fields[cur_output_fields] = extra_device->fields[c];
            ++cur_output_fields;
        }
    }

    *num_fields = num_output_fields;
    return output_fields;
}

static char *arg_param(char *arg)
{
    char *p = strchr(arg, ':');
    if (p) {
        return ++p;
    } else {
        return p;
    }
}

static FILE *fopen_output(char *param)
{
    FILE *file;
    if (!param || !*param) {
        return stdout;
    }
    file = fopen(param, "a");
    if (!file) {
        fprintf(stderr, "rtl_433: failed to open output file\n");
        exit(1);
    }
    return file;
}

// e.g. ":514", "localhost", "[::1]", "127.0.0.1:514", "[::1]:514"
static void hostport_param(char *param, char **host, char **port)
{
    if (param && *param) {
        if (*param != ':') {
            *host = param;
            if (*param == '[') {
                (*host)++;
                param = strchr(param, ']');
                if (param) {
                    *param++ = '\0';
                } else {
                    fprintf(stderr, "Malformed Ipv6 address!\n");
                    exit(1);
                }
            }
        }
        param = strchr(param, ':');
        if (param) {
            *param++ = '\0';
            *port = param;
        }
    }
}

static void add_json_output(char *param)
{
    cfg.output_handler[cfg.last_output_handler++] = data_output_json_create(fopen_output(param));
}

static void add_csv_output(char *param, r_device *devices, int num_devices, r_device *extra_device)
{
    int num_output_fields;
    const char **output_fields = determine_csv_fields(devices, num_devices, extra_device, &num_output_fields);
    cfg.output_handler[cfg.last_output_handler++] = data_output_csv_create(fopen_output(param), output_fields, num_output_fields);
    free(output_fields);
}

static void add_kv_output(char *param)
{
    cfg.output_handler[cfg.last_output_handler++] = data_output_kv_create(fopen_output(param));
}

static void add_syslog_output(char *param)
{
    char *host = "localhost";
    char *port = "514";
    hostport_param(param, &host, &port);
    fprintf(stderr, "Syslog UDP datagrams to %s port %s\n", host, port);

    cfg.output_handler[cfg.last_output_handler++] = data_output_syslog_create(host, port);
}

static void add_null_output(char *param)
{
    cfg.output_handler[cfg.last_output_handler++] = NULL;
}

static void add_dumper(char const *spec, file_info_t *dumper, int overwrite)
{
    while (dumper->spec && *dumper->spec) ++dumper; // TODO: check MAX_DUMP_OUTPUTS

    parse_file_info(spec, dumper);
    if (strcmp(dumper->path, "-") == 0) { /* Write samples to stdout */
        dumper->file = stdout;
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
    } else {
        if (access(dumper->path, F_OK) == 0 && !overwrite) {
            fprintf(stderr, "Output file %s already exists, exiting\n", spec);
            exit(1);
        }
        dumper->file = fopen(dumper->path, "wb");
        if (!dumper->file) {
            fprintf(stderr, "Failed to open %s\n", spec);
            exit(1);
        }
    }
    if (dumper->format == VCD_LOGIC) {
        pulse_data_print_vcd_header(dumper->file, cfg.samp_rate);
    }
}

int main(int argc, char **argv) {
#ifndef _WIN32
    struct sigaction sigact;
#endif
    char *dev_query = NULL;
    char *test_data = NULL;
    char *out_filename = NULL;
    char *in_filename = NULL;
    FILE *in_file;
    int r = 0, opt;
    char *gain_str = NULL;
    int i;
    int ppm_error = 0;
    struct dm_state *demod;
    int frequency_current = 0;
    uint32_t out_block_size = DEFAULT_BUF_LENGTH;
    int have_opt_R = 0;
    int register_all = 0;
    r_device *flex_device = NULL;

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    demod = calloc(1, sizeof(struct dm_state));
    cfg.demod = demod;

    /* initialize tables */
    baseband_init();

    r_device devices[] = {
#define DECL(name) name,
            DEVICES
#undef DECL
            };

    cfg.num_r_devices = sizeof(devices) / sizeof(*devices);

    demod->level_limit = DEFAULT_LEVEL_LIMIT;
    demod->hop_time = DEFAULT_HOP_TIME;

    while ((opt = getopt(argc, argv, "hVx:z:p:DtaAI:qm:M:r:w:W:l:d:f:H:g:s:b:n:R:X:F:C:T:UGy:E")) != -1) {
        if (optarg && (!strcmp(optarg, "help") || !strcmp(optarg, "?")))
            opt = '?'; // fall through to help
        switch (opt) {
            case 'h':
                usage(NULL, 0);
                break;
            case 'V':
                version();
                break;
            case 'd':
                dev_query = optarg;
                break;
            case 'f':
                if (cfg.frequencies < MAX_PROTOCOLS) cfg.frequency[cfg.frequencies++] = atouint32_metric(optarg, "-f: ");
                else fprintf(stderr, "Max number of frequencies reached %d\n", MAX_PROTOCOLS);
                break;
            case 'H':
                demod->hop_time = atoi_time(optarg, "-H: ");
                break;
            case 'g':
                gain_str = optarg;
                break;
            case 'G':
                register_all = 1;
                break;
            case 'p':
                ppm_error = atoi(optarg);
                break;
            case 's':
                cfg.samp_rate = atouint32_metric(optarg, "-s: ");
                break;
            case 'b':
                out_block_size = atouint32_metric(optarg, "-b: ");
                break;
            case 'l':
                demod->level_limit = atouint32_metric(optarg, "-l: ");
                break;
            case 'n':
                cfg.bytes_to_read = atouint32_metric(optarg, "-n: ") * 2;
                break;
            case 'a':
                if (!demod->am_analyze)
                    demod->am_analyze = am_analyze_create();
                break;
            case 'A':
                demod->analyze_pulses = 1;
                break;
            case 'I':
                cfg.include_only = atoi(optarg);
                break;
            case 'r':
                in_filename = optarg;
                // TODO: check_read_file_info()
                break;
            case 'w':
                add_dumper(optarg, demod->dumper, 0);
                break;
            case 'W':
                add_dumper(optarg, demod->dumper, 1);
                break;
            case 't':
                if (demod->am_analyze)
                    am_analyze_enable_grabber(demod->am_analyze, SIGNAL_GRABBER_BUFFER);
                break;
            case 'm':
                fprintf(stderr, "sample mode option is deprecated.\n");
                usage(NULL, 1);
                break;
            case 'M':
                cfg.report_meta = atoi(optarg);
                break;
            case 'D':
                debug_output++;
                break;
            case 'z':
                if (demod->am_analyze)
                    demod->am_analyze->override_short = atoi(optarg);
                break;
            case 'x':
                if (demod->am_analyze)
                    demod->am_analyze->override_long = atoi(optarg);
                break;
            case 'R':
                if (!have_opt_R) {
                    for (i = 0; i < cfg.num_r_devices; i++) {
                        devices[i].disabled = 1;
                    }
                    have_opt_R = 1;
                }

                i = atoi(optarg);
                if (i > cfg.num_r_devices) {
                    fprintf(stderr, "Remote device number specified larger than number of devices\n\n");
                    usage(devices, 1);
                }

                if (i >= 1) {
                    devices[i - 1].disabled = 0;
                } else {
                    fprintf(stderr, "Disabling all device decoders.\n");
                }
                break;
            case 'X':
                flex_device = flex_create_device(optarg);
                register_protocol(demod, flex_device);
                if (flex_device->modulation >= FSK_DEMOD_MIN_VAL) {
                    demod->enable_FM_demod = 1;
                }
                break;
            case 'q':
                cfg.quiet_mode = 1;
                break;
            case 'F':
                if (strncmp(optarg, "json", 4) == 0) {
                    add_json_output(arg_param(optarg));
                } else if (strncmp(optarg, "csv", 3) == 0) {
                    add_csv_output(arg_param(optarg), devices, cfg.num_r_devices, flex_device);
                } else if (strncmp(optarg, "kv", 2) == 0) {
                    add_kv_output(arg_param(optarg));
                } else if (strncmp(optarg, "syslog", 6) == 0) {
                    add_syslog_output(arg_param(optarg));
                } else if (strncmp(optarg, "null", 4) == 0) {
                    add_null_output(arg_param(optarg));
                } else {
                    fprintf(stderr, "Invalid output format %s\n", optarg);
                    usage(NULL, 1);
                }
                break;
            case 'C':
                if (strcmp(optarg, "native") == 0) {
                    cfg.conversion_mode = CONVERT_NATIVE;
                } else if (strcmp(optarg, "si") == 0) {
                    cfg.conversion_mode = CONVERT_SI;
                } else if (strcmp(optarg, "customary") == 0) {
                    cfg.conversion_mode = CONVERT_CUSTOMARY;
                } else {
                    fprintf(stderr, "Invalid conversion mode %s\n", optarg);
                    usage(NULL, 1);
                }
                break;
            case 'U':
#ifdef _WIN32
                putenv("TZ=UTC+0");
                _tzset();
#else
                cfg.utc_mode = setenv("TZ", "UTC", 1);
                if (cfg.utc_mode != 0)
                    fprintf(stderr, "Unable to set TZ to UTC; error code: %d\n", cfg.utc_mode);
#endif
                break;
            case 'T':
                cfg.duration = atoi_time(optarg, "-T: ");
                if (cfg.duration < 1) {
                    fprintf(stderr, "Duration '%s' not a positive number; will continue indefinitely\n", optarg);
                }
                break;
            case 'y':
                test_data = optarg;
                break;
            case 'E':
                cfg.stop_after_successful_events_flag = 1;
                break;
            default:
                // handle missing arguments as help request
                if (optopt == 'R') usage(devices, 0);
                else if (optopt == 'X') flex_create_device(NULL);
                else if (optopt == 'd') help_device();
                else if (optopt == 'g') help_gain();
                else if (optopt == 'F') help_output();
                else if (optopt == 'r') help_read();
                else if (optopt == 'w') help_write();
                else if (optopt == 'W') help_write();
                else usage(NULL, 1);
                break;
        }
    }

    if (demod->am_analyze) {
        demod->am_analyze->level_limit = &demod->level_limit;
        demod->am_analyze->frequency = &cfg.center_frequency;
        demod->am_analyze->samp_rate = &cfg.samp_rate;
        demod->am_analyze->sample_size = &demod->sample_size;
    }

    if (argc <= optind - 1) {
        usage(NULL, 1);
    } else {
        out_filename = argv[optind]; // deprecated
    }

    if (cfg.last_output_handler < 1) {
        add_kv_output(NULL);
    }

    for (i = 0; i < cfg.num_r_devices; i++) {
        devices[i].protocol_num = i + 1;
        // if not disabled or register all and not hidden
        if (!devices[i].disabled || (register_all && devices[i].disabled == 1)) {
            register_protocol(demod, &devices[i]);
            if (devices[i].modulation >= FSK_DEMOD_MIN_VAL) {
              demod->enable_FM_demod = 1;
            }
        }
    }

    if (!cfg.quiet_mode)
        fprintf(stderr,"Registered %d out of %d device decoding protocols\n",
                demod->r_dev_num, cfg.num_r_devices);

    if (out_block_size < MINIMAL_BUF_LENGTH ||
            out_block_size > MAXIMAL_BUF_LENGTH) {
        fprintf(stderr,
                "Output block size wrong value, falling back to default\n");
        fprintf(stderr,
                "Minimal length: %u\n", MINIMAL_BUF_LENGTH);
        fprintf(stderr,
                "Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
        out_block_size = DEFAULT_BUF_LENGTH;
    }

    if (test_data) {
        r = 0;
        for (i = 0; i < demod->r_dev_num; i++) {
            if (!cfg.quiet_mode)
                fprintf(stderr, "Verifying test data with device %s.\n", demod->r_devs[i]->name);
            r += pulse_demod_string(test_data, demod->r_devs[i]);
        }
        exit(!r);
    }

    if (!in_filename) {
        r = sdr_open(&cfg.dev, &demod->sample_size, dev_query, !cfg.quiet_mode);
        if (r < 0) {
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
        SetConsoleCtrlHandler((PHANDLER_ROUTINE) sighandler, TRUE);
#endif
        /* Set the sample rate */
        r = sdr_set_sample_rate(cfg.dev, cfg.samp_rate, !cfg.quiet_mode);

        fprintf(stderr, "Bit detection level set to %d%s.\n", demod->level_limit, (demod->level_limit ? "" : " (Auto)"));

        /* Enable automatic gain if gain_str empty (or 0 for RTL-SDR), set manual gain otherwise */
        r = sdr_set_tuner_gain(cfg.dev, gain_str, !cfg.quiet_mode);

        if (ppm_error)
            r = sdr_set_freq_correction(cfg.dev, ppm_error, !cfg.quiet_mode);
    }

    if (out_filename) {
        add_dumper(out_filename, demod->dumper, 0); // deprecated
    }

    if (in_filename) {
        parse_file_info(in_filename, &demod->load_info);
        unsigned char *test_mode_buf = malloc(DEFAULT_BUF_LENGTH * sizeof(unsigned char));
        float *test_mode_float_buf = malloc(DEFAULT_BUF_LENGTH / sizeof(int16_t) * sizeof(float));
        if (!test_mode_buf || !test_mode_float_buf)
        {
            fprintf(stderr, "Couldn't allocate read buffers!\n");
            exit(1);
        }
        if (strcmp(demod->load_info.path, "-") == 0) { /* read samples from stdin */
            in_file = stdin;
            in_filename = "<stdin>";
        } else {
            in_file = fopen(demod->load_info.path, "rb");
            if (!in_file) {
                fprintf(stderr, "Opening file: %s failed!\n", in_filename);
                goto out;
            }
        }
        fprintf(stderr, "Test mode active. Reading samples from file: %s\n", in_filename);  // Essential information (not quiet)
        if (demod->load_info.format == CU8_IQ
                || demod->load_info.format == S16_AM
                || demod->load_info.format == S16_FM) {
            demod->sample_size = sizeof(uint8_t); // CU8, AM, FM
        } else {
            demod->sample_size = sizeof(int16_t); // CF32, CS16
        }
        if (!cfg.quiet_mode) {
            fprintf(stderr, "Input format: %s\n", file_info_string(&demod->load_info));
        }
        sample_file_pos = 0.0;

        int n_blocks = 0;
        unsigned long n_read;
        do {
            if (demod->load_info.format == CF32_IQ) {
                n_read = fread(test_mode_float_buf, sizeof(float), DEFAULT_BUF_LENGTH / 2, in_file);
                // clamp float to [-1,1] and scale to Q0.15
                for(unsigned long n = 0; n < n_read; n++) {
                    int s_tmp = test_mode_float_buf[n] * INT16_MAX;
                    if (s_tmp < -INT16_MAX)
                        s_tmp = -INT16_MAX;
                    else if (s_tmp > INT16_MAX)
                        s_tmp = INT16_MAX;
                    test_mode_buf[n] = (int16_t)s_tmp;
                }
            } else {
                n_read = fread(test_mode_buf, 1, DEFAULT_BUF_LENGTH, in_file);
            }
            if (n_read == 0) break;  // sdr_callback() will Segmentation Fault with len=0
            sdr_callback(test_mode_buf, n_read, demod);
            n_blocks++;
            sample_file_pos = (float)n_blocks * n_read / cfg.samp_rate / 2 / demod->sample_size;
        } while (n_read != 0);

        // Call a last time with cleared samples to ensure EOP detection
        memset(test_mode_buf, 128, DEFAULT_BUF_LENGTH);  // 128 is 0 in unsigned data
        sdr_callback(test_mode_buf, DEFAULT_BUF_LENGTH, demod);

        //Always classify a signal at the end of the file
        if (demod->am_analyze)
            am_analyze_classify(demod->am_analyze);
        if (!cfg.quiet_mode) {
            fprintf(stderr, "Test mode file issued %d packets\n", n_blocks);
        }
        free(test_mode_buf);
        free(test_mode_float_buf);
        exit(0);
    }

    /* Reset endpoint before we start reading from it (mandatory) */
    r = sdr_reset(cfg.dev, !cfg.quiet_mode);
    if (r < 0)
        fprintf(stderr, "WARNING: Failed to reset buffers.\n");
    r = sdr_activate(cfg.dev);

    if (cfg.frequencies == 0) {
        cfg.frequency[0] = DEFAULT_FREQUENCY;
        cfg.frequencies = 1;
    } else {
        time(&cfg.rawtime_old);
    }
    if (!cfg.quiet_mode) {
        fprintf(stderr, "Reading samples in async mode...\n");
    }
    if (cfg.duration > 0) {
        time(&cfg.stop_time);
        cfg.stop_time += cfg.duration;
    }
    while (!cfg.do_exit) {
        /* Set the cfg.frequency */
        cfg.center_frequency = cfg.frequency[frequency_current];
        r = sdr_set_center_freq(cfg.dev, cfg.center_frequency, !cfg.quiet_mode);

#ifndef _WIN32
        signal(SIGALRM, sighandler);
        alarm(3); // require callback to run every 3 second, abort otherwise
#endif
        r = sdr_start(cfg.dev, sdr_callback, (void *) demod,
                DEFAULT_ASYNC_BUF_NUMBER, out_block_size);
        if (r < 0) {
            fprintf(stderr, "WARNING: async read failed (%i).\n", r);
            break;
        }
#ifndef _WIN32
        alarm(0); // cancel the watchdog timer
#endif
        cfg.do_exit_async = 0;
        frequency_current = (frequency_current + 1) % cfg.frequencies;
    }

    if (!cfg.do_exit)
        fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

    for (file_info_t const *dumper = demod->dumper; dumper->spec && *dumper->spec; ++dumper)
        if (dumper->file && (dumper->file != stdout))
            fclose(dumper->file);

    r = sdr_deactivate(cfg.dev);

    for (i = 0; i < demod->r_dev_num; i++)
        free(demod->r_devs[i]);

    if (demod->am_analyze)
        am_analyze_free(demod->am_analyze);

    free(demod);

    sdr_close(cfg.dev);
out:
    for (int i = 0; i < cfg.last_output_handler; ++i) {
        data_output_free(cfg.output_handler[i]);
    }
    return r >= 0 ? r : -r;
}
