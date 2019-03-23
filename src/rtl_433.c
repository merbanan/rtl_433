/** @file
    rtl_433, turns your Realtek RTL2832 based DVB dongle into a 433.92MHz generic data receiver.

    Copyright (C) 2012 by Benjamin Larsson <benjamin@southpole.se>

    Based on rtl_sdr
    Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <signal.h>

#include "rtl_433.h"
#include "r_device.h"
#include "rtl_433_devices.h"
#include "sdr.h"
#include "baseband.h"
#include "pulse_detect.h"
#include "pulse_demod.h"
#include "decoder.h"
#include "data.h"
#include "r_util.h"
#include "optparse.h"
#include "fileformat.h"
#include "samp_grab.h"
#include "am_analyze.h"
#include "confparse.h"
#include "compat_paths.h"
#include "compat_time.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#ifdef _MSC_VER
#define F_OK 0
#endif
#endif
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifndef _MSC_VER
#include <getopt.h>
#else
#include "getopt/getopt.h"
#endif

char const *version_string(void)
{
    return "rtl_433"
#ifdef GIT_VERSION
#define STR_VALUE(arg) #arg
#define STR_EXPAND(s) STR_VALUE(s)
            " version " STR_EXPAND(GIT_VERSION)
            " branch " STR_EXPAND(GIT_BRANCH)
            " at " STR_EXPAND(GIT_TIMESTAMP)
#undef STR_VALUE
#undef STR_EXPAND
#else
            " version unknown"
#endif
            " inputs file rtl_tcp"
#ifdef RTLSDR
            " RTL-SDR"
#endif
#ifdef SOAPYSDR
            " SoapySDR"
#endif
            ;
}

r_device *flex_create_device(char *spec); // maybe put this in some header file?

void data_acquired_handler(r_device *r_dev, data_t *data);

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
    pulse_detect_t *pulse_detect;
    filter_state_t lowpass_filter_state;
    demodfm_state_t demod_FM_state;
    int enable_FM_demod;
    samp_grab_t *samp_grab;
    am_analyze_t *am_analyze;
    int analyze_pulses;
    file_info_t load_info;
    list_t dumper;
    int hop_time;

    /* Protocol states */
    list_t r_devs;

    pulse_data_t    pulse_data;
    pulse_data_t    fsk_pulse_data;
    unsigned frame_event_count;
    unsigned frame_start_ago;
    unsigned frame_end_ago;
    struct timeval now;
    float sample_file_pos;
};

static void print_version(void)
{
    fprintf(stderr, "%s\n", version_string());
}

static void usage(int exit_code)
{
    fprintf(stderr,
            "Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.\n"
            "\nUsage:\t\t= General options =\n"
            "  [-V] Output the version string and exit\n"
            "  [-v] Increase verbosity (can be used multiple times).\n"
            "       -v : verbose, -vv : verbose decoders, -vvv : debug decoders, -vvvv : trace decoding).\n"
            "  [-c <path>] Read config options from a file\n"
            "\t\t= Tuner options =\n"
            "  [-d <RTL-SDR USB device index> | :<RTL-SDR USB device serial> | <SoapySDR device query> | rtl_tcp | help]\n"
            "  [-g <gain> | help] (default: auto)\n"
            "  [-t <settings>] apply a list of keyword=value settings for SoapySDR devices\n"
            "       e.g. -t \"antenna=A,bandwidth=4.5M,rfnotch_ctrl=false\"\n"
            "  [-f <frequency>] [-f...] Receive frequency(s) (default: %i Hz)\n"
            "  [-H <seconds>] Hop interval for polling of multiple frequencies (default: %i seconds)\n"
            "  [-p <ppm_error] Correct rtl-sdr tuner frequency offset error (default: 0)\n"
            "  [-s <sample rate>] Set sample rate (default: %i Hz)\n"
            "\t\t= Demodulator options =\n"
            "  [-R <device> | help] Enable only the specified device decoding protocol (can be used multiple times)\n"
            "       Specify a negative number to disable a device decoding protocol (can be used multiple times)\n"
            "  [-G] Enable all device protocols, included those disabled by default\n"
            "  [-X <spec> | help] Add a general purpose decoder (-R 0 to disable all other decoders)\n"
            "  [-l <level>] Change detection level used to determine pulses [0-16384] (0 = auto) (default: %i)\n"
            "  [-z <value>] Override short value in data decoder\n"
            "  [-x <value>] Override long value in data decoder\n"
            "  [-n <value>] Specify number of samples to take (each sample is 2 bytes: 1 each of I & Q)\n"
            "\t\t= Analyze/Debug options =\n"
            "  [-a] Analyze mode. Print a textual description of the signal.\n"
            "  [-A] Pulse Analyzer. Enable pulse analysis and decode attempt.\n"
            "       Disable all decoders with -R 0 if you want analyzer output only.\n"
            "  [-y <code>] Verify decoding of demodulated test data (e.g. \"{25}fb2dd58\") with enabled devices\n"
            "\t\t= File I/O options =\n"
            "  [-S none | all | unknown | known] Signal auto save. Creates one file per signal.\n"
            "       Note: Saves raw I/Q samples (uint8 pcm, 2 channel). Preferred mode for generating test files.\n"
            "  [-r <filename> | help] Read data from input file instead of a receiver\n"
            "  [-w <filename> | help] Save data stream to output file (a '-' dumps samples to stdout)\n"
            "  [-W <filename> | help] Save data stream to output file, overwrite existing file\n"
            "\t\t= Data output options =\n"
            "  [-F kv | json | csv | syslog | null | help] Produce decoded output in given format.\n"
            "       Append output to file with :<filename> (e.g. -F csv:log.csv), defaults to stdout.\n"
            "       Specify host/port for syslog with e.g. -F syslog:127.0.0.1:1514\n"
            "  [-M time | reltime | notime | hires | utc | protocol | level | bits | help] Add various meta data to each output.\n"
            "  [-K FILE | PATH | <tag>] Add an expanded token or fixed tag to every output line.\n"
            "  [-C native | si | customary] Convert units in decoded output.\n"
            "  [-T <seconds>] Specify number of seconds to run\n"
            "  [-E] Stop after outputting successful event(s)\n"
            "  [-h] Output this usage help and exit\n"
            "       Use -d, -g, -R, -X, -F, -M, -r, -w, or -W without argument for more help\n\n",
            DEFAULT_FREQUENCY, DEFAULT_HOP_TIME, DEFAULT_SAMPLE_RATE, DEFAULT_LEVEL_LIMIT);
    exit(exit_code);
}

static void help_protocols(r_device *devices, unsigned num_devices, int exit_code)
{
    unsigned i;
    char disabledc;

    if (devices) {
        fprintf(stderr, "Supported device protocols:\n");
        for (i = 0; i < num_devices; i++) {
            disabledc = devices[i].disabled ? '*' : ' ';
            if (devices[i].disabled <= 2) // if not hidden
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
            "\tTo set gain for RTL-SDR use -g <gain> to set an overall gain in dB.\n"
#ifdef SOAPYSDR
            "\tSoapySDR device driver is available.\n"
#else
            "\tSoapySDR device driver is not available.\n"
#endif
            "[-d \"\" Open default SoapySDR device\n"
            "[-d driver=rtlsdr Open e.g. specific SoapySDR device\n"
            "\tTo set gain for SoapySDR use -g ELEM=val,ELEM=val,... e.g. -g LNA=20,TIA=8,PGA=2 (for LimeSDR).\n"
            "[-d rtl_tcp[:[//]host[:port]] (default: localhost:1234)\n"
            "\tSpecify host/port to connect to with e.g. -d rtl_tcp:127.0.0.1:1234\n");
    exit(0);
}

static void help_gain(void)
{
    fprintf(stderr,
            "-g <gain>] (default: auto)\n"
            "\tFor RTL-SDR: gain in dB (\"0\" is auto).\n"
            "\tFor SoapySDR: gain in dB for automatic distribution (\"\" is auto), or string of gain elements.\n"
            "\tE.g. \"LNA=20,TIA=8,PGA=2\" for LimeSDR.\n");
    exit(0);
}

static void help_output(void)
{
    fprintf(stderr,
            "[-F kv|json|csv|syslog|null] Produce decoded output in given format.\n"
            "\tWithout this option the default is KV output. Use \"-F null\" to remove the default.\n"
            "\tAppend output to file with :<filename> (e.g. -F csv:log.csv), defaults to stdout.\n"
            "\tSpecify host/port for syslog with e.g. -F syslog:127.0.0.1:1514\n");
    exit(0);
}

static void help_meta(void)
{
    fprintf(stderr,
            "[-M time|reltime|notime|hires|level] Add various metadata to every output line.\n"
            "\tUse \"time\" to add current date and time meta data (preset for live inputs).\n"
            "\tUse \"reltime\" to add sample position meta data (preset for read-file and stdin).\n"
            "\tUse \"notime\" to remove time meta data.\n"
            "\tUse \"hires\" to add microsecods to date time meta data.\n"
            "\tUse \"utc\" / \"noutc\" to output timestamps in UTC.\n"
            "\t\t(this may also be accomplished by invocation with TZ environment variable set).\n"
            "\tUse \"protocol\" / \"noprotocol\" to output the decoder protocol number meta data.\n"
            "\tUse \"level\" to add Modulation, Frequency, RSSI, SNR, and Noise meta data.\n"
            "\tUse \"bits\" to add bit representation to code outputs (for debug).\n"
            "\nNote:"
            "\tUse \"newmodel\" to transition to new model keys. This will become the default someday.\n"
            "\tA table of changes and discussion is at https://github.com/merbanan/rtl_433/pull/986.\n\n");
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
            "\t'i.f32', 'q.f32', 'logic.u8', 'ook', and 'vcd'.\n\n"
            "\tParameters must be separated by non-alphanumeric chars and are case-insensitive.\n"
            "\tOverrides can be prefixed, separated by colon (':')\n\n"
            "\tE.g. default detection by extension: path/filename.am.s16\n"
            "\tforced overrides: am:s16:path/filename.ext\n");
    exit(0);
}

static void update_protocol(r_cfg_t *cfg, r_device *r_dev)
{
    float samples_per_us = cfg->samp_rate / 1.0e6;

    r_dev->f_short_width = 1.0 / (r_dev->short_width * samples_per_us);
    r_dev->f_long_width  = 1.0 / (r_dev->long_width * samples_per_us);
    r_dev->s_short_width = r_dev->short_width * samples_per_us;
    r_dev->s_long_width  = r_dev->long_width * samples_per_us;
    r_dev->s_reset_limit = r_dev->reset_limit * samples_per_us;
    r_dev->s_gap_limit   = r_dev->gap_limit * samples_per_us;
    r_dev->s_sync_width  = r_dev->sync_width * samples_per_us;
    r_dev->s_tolerance   = r_dev->tolerance * samples_per_us;

    r_dev->verbose      = cfg->verbosity > 0 ? cfg->verbosity - 1 : 0;
    r_dev->verbose_bits = cfg->verbose_bits;

    r_dev->new_model_keys = cfg->new_model_keys; // TODO: temporary allow to change to new style model keys
}

static void register_protocol(r_cfg_t *cfg, r_device *r_dev, char *arg)
{
    r_device *p;
    if (r_dev->create_fn) {
        p = r_dev->create_fn(arg);
    }
    else {
        if (arg && *arg) {
            fprintf(stderr, "Protocol [%d] \"%s\" does not take arguments \"%s\"!\n", r_dev->protocol_num, r_dev->name, arg);
        }
        p = malloc(sizeof (*p));
        *p = *r_dev; // copy
    }

    update_protocol(cfg, p);

    p->output_fn  = data_acquired_handler;
    p->output_ctx = cfg;

    list_push(&cfg->demod->r_devs, p);

    if (cfg->verbosity) {
        fprintf(stderr, "Registering protocol [%d] \"%s\"\n", r_dev->protocol_num, r_dev->name);
    }
}

static void free_protocol(r_device *r_dev)
{
    // free(r_dev->name);
    free(r_dev->decode_ctx);
    free(r_dev);
}

static void unregister_protocol(r_cfg_t *cfg, r_device *r_dev)
{
    for (size_t i = 0; i < cfg->demod->r_devs.len; ++i) { // list might contain NULLs
        r_device *p = cfg->demod->r_devs.elems[i];
        if (!strcmp(p->name, r_dev->name)) {
            list_remove(&cfg->demod->r_devs, i, (list_elem_free_fn)free_protocol);
            i--; // so we don't skip the next elem now shifted down
        }
    }
}

static void register_all_protocols(r_cfg_t *cfg, unsigned disabled)
{
    for (int i = 0; i < cfg->num_r_devices; i++) {
        // register all device protocols that are not disabled
        if (cfg->devices[i].disabled <= disabled) {
            register_protocol(cfg, &cfg->devices[i], NULL);
        }
    }
}

static void update_protocols(r_cfg_t *cfg)
{
    float samples_per_us = cfg->samp_rate / 1.0e6;

    for (void **iter = cfg->demod->r_devs.elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        update_protocol(cfg, r_dev);
    }
}

static void calc_rssi_snr(r_cfg_t *cfg, pulse_data_t *pulse_data)
{
    float asnr   = (float)pulse_data->ook_high_estimate / ((float)pulse_data->ook_low_estimate + 1);
    float foffs1 = (float)pulse_data->fsk_f1_est / INT16_MAX * cfg->samp_rate / 2.0;
    float foffs2 = (float)pulse_data->fsk_f2_est / INT16_MAX * cfg->samp_rate / 2.0;
    pulse_data->freq1_hz = (foffs1 + cfg->center_frequency);
    pulse_data->freq2_hz = (foffs2 + cfg->center_frequency);
    // NOTE: for (CU8) amplitude is 10x (because it's squares)
    if (cfg->demod->sample_size == 1) { // amplitude (CU8)
        pulse_data->rssi_db = 10.0f * log10f(pulse_data->ook_high_estimate) - 42.1442f; // 10*log10f(16384.0f)
        pulse_data->noise_db = 10.0f * log10f(pulse_data->ook_low_estimate + 1) - 42.1442f; // 10*log10f(16384.0f)
        pulse_data->snr_db  = 10.0f * log10f(asnr);
    } else { // magnitude (CS16)
        pulse_data->rssi_db = 20.0f * log10f(pulse_data->ook_high_estimate) - 84.2884f; // 20*log10f(16384.0f)
        pulse_data->noise_db = 20.0f * log10f(pulse_data->ook_low_estimate + 1) - 84.2884f; // 20*log10f(16384.0f)
        pulse_data->snr_db  = 20.0f * log10f(asnr);
    }
}

static char *time_pos_str(r_cfg_t *cfg, unsigned samples_ago, char *buf)
{
    if (cfg->report_time == REPORT_TIME_SAMPLES) {
        double s_per_sample = 1.0 / cfg->samp_rate;
        return sample_pos_str(cfg->demod->sample_file_pos - samples_ago * s_per_sample, buf);
    }
    else {
        struct timeval ago = cfg->demod->now;
        double us_per_sample = 1e6 / cfg->samp_rate;
        unsigned usecs_ago   = samples_ago * us_per_sample;
        while (ago.tv_usec < (int)usecs_ago) {
            ago.tv_sec -= 1;
            ago.tv_usec += 1000000;
        }
        ago.tv_usec -= usecs_ago;

        if (cfg->report_time_hires)
            return usecs_time_str(&ago, buf);
        else
            return local_time_str(ago.tv_sec, buf);
    }
}

/** Pass the data structure to all output handlers. Frees data afterwards. */
void data_acquired_handler(r_device *r_dev, data_t *data)
{
    r_cfg_t *cfg = r_dev->output_ctx;

    // replace textual battery key with numerical battery key
    if (cfg->new_model_keys) {
        for (data_t *d = data; d; d = d->next) {
            if ((d->type == DATA_STRING) && !strcmp(d->key, "battery")) {
                free(d->key);
                d->key = strdup("battery_ok");
                int ok = d->value && !strcmp(d->value, "OK");
                free(d->value);
                d->type = DATA_INT;
                d->value = malloc(sizeof(int));
                *(int *)d->value = ok;
                break;
            }
        }
    }

    if (cfg->conversion_mode == CONVERT_SI) {
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
    if (cfg->conversion_mode == CONVERT_CUSTOMARY) {
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

    // prepend "description" if requested
    if (cfg->report_description) {
        data = data_prepend(data,
                "description", "Description", DATA_STRING, r_dev->name,
                NULL);
    }

    // prepend "protocol" if requested
    if (cfg->report_protocol && r_dev->protocol_num) {
        data = data_prepend(data,
                "protocol", "Protocol", DATA_INT, r_dev->protocol_num,
                NULL);
    }

    if (cfg->report_meta && cfg->demod->fsk_pulse_data.fsk_f2_est) {
        data_append(data,
                "mod",   "Modulation",  DATA_STRING, "FSK",
                "freq1", "Freq1",       DATA_FORMAT, "%.1f MHz", DATA_DOUBLE, cfg->demod->fsk_pulse_data.freq1_hz / 1000000.0,
                "freq2", "Freq2",       DATA_FORMAT, "%.1f MHz", DATA_DOUBLE, cfg->demod->fsk_pulse_data.freq2_hz / 1000000.0,
                "rssi",  "RSSI",        DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg->demod->fsk_pulse_data.rssi_db,
                "snr",   "SNR",         DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg->demod->fsk_pulse_data.snr_db,
                "noise", "Noise",       DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg->demod->fsk_pulse_data.noise_db,
                NULL);
    }
    else if (cfg->report_meta) {
        data_append(data,
                "mod",   "Modulation",  DATA_STRING, "ASK",
                "freq",  "Freq",        DATA_FORMAT, "%.1f MHz", DATA_DOUBLE, cfg->demod->pulse_data.freq1_hz / 1000000.0,
                "rssi",  "RSSI",        DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg->demod->pulse_data.rssi_db,
                "snr",   "SNR",         DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg->demod->pulse_data.snr_db,
                "noise", "Noise",       DATA_FORMAT, "%.1f dB", DATA_DOUBLE, cfg->demod->pulse_data.noise_db,
                NULL);
    }

    // prepend "time" if requested
    if (cfg->report_time != REPORT_TIME_OFF) {
        char time_str[LOCAL_TIME_BUFLEN];
        time_pos_str(cfg, cfg->demod->pulse_data.start_ago, time_str);
        data = data_prepend(data,
                "time", "", DATA_STRING, time_str,
                NULL);
    }

    // prepend "tag" if available
    if (cfg->output_tag) {
        char const *output_tag = cfg->output_tag;
        if (cfg->in_filename && !strcmp("PATH", cfg->output_tag)) {
            output_tag = cfg->in_filename;
        }
        else if (cfg->in_filename && !strcmp("FILE", cfg->output_tag)) {
            output_tag = file_basename(cfg->in_filename);
        }
        data = data_prepend(data,
                "tag", "Tag", DATA_STRING, output_tag,
                NULL);
    }

    for (size_t i = 0; i < cfg->output_handler.len; ++i) { // list might contain NULLs
        data_output_print(cfg->output_handler.elems[i], data);
    }
    data_free(data);
}

static int run_ook_demods(list_t *r_devs, pulse_data_t *pulse_data)
{
    int p_events = 0;

    for (void **iter = r_devs->elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        switch (r_dev->modulation) {
        case OOK_PULSE_PCM_RZ:
            p_events += pulse_demod_pcm(pulse_data, r_dev);
            break;
        case OOK_PULSE_PPM:
            p_events += pulse_demod_ppm(pulse_data, r_dev);
            break;
        case OOK_PULSE_PWM:
            p_events += pulse_demod_pwm(pulse_data, r_dev);
            break;
        case OOK_PULSE_MANCHESTER_ZEROBIT:
            p_events += pulse_demod_manchester_zerobit(pulse_data, r_dev);
            break;
        case OOK_PULSE_PIWM_RAW:
            p_events += pulse_demod_piwm_raw(pulse_data, r_dev);
            break;
        case OOK_PULSE_PIWM_DC:
            p_events += pulse_demod_piwm_dc(pulse_data, r_dev);
            break;
        case OOK_PULSE_DMC:
            p_events += pulse_demod_dmc(pulse_data, r_dev);
            break;
        case OOK_PULSE_PWM_OSV1:
            p_events += pulse_demod_osv1(pulse_data, r_dev);
            break;
        // FSK decoders
        case FSK_PULSE_PCM:
        case FSK_PULSE_PWM:
            break;
        case FSK_PULSE_MANCHESTER_ZEROBIT:
            p_events += pulse_demod_manchester_zerobit(pulse_data, r_dev);
            break;
        default:
            fprintf(stderr, "Unknown modulation %d in protocol!\n", r_dev->modulation);
        }
    }

    return p_events;
}

static int run_fsk_demods(list_t *r_devs, pulse_data_t *fsk_pulse_data)
{
    int p_events = 0;

    for (void **iter = r_devs->elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        switch (r_dev->modulation) {
        // OOK decoders
        case OOK_PULSE_PCM_RZ:
        case OOK_PULSE_PPM:
        case OOK_PULSE_PWM:
        case OOK_PULSE_MANCHESTER_ZEROBIT:
        case OOK_PULSE_PIWM_RAW:
        case OOK_PULSE_PIWM_DC:
        case OOK_PULSE_DMC:
        case OOK_PULSE_PWM_OSV1:
            break;
        case FSK_PULSE_PCM:
            p_events += pulse_demod_pcm(fsk_pulse_data, r_dev);
            break;
        case FSK_PULSE_PWM:
            p_events += pulse_demod_pwm(fsk_pulse_data, r_dev);
            break;
        case FSK_PULSE_MANCHESTER_ZEROBIT:
            p_events += pulse_demod_manchester_zerobit(fsk_pulse_data, r_dev);
            break;
        default:
            fprintf(stderr, "Unknown modulation %d in protocol!\n", r_dev->modulation);
        }
    }

    return p_events;
}

static void sdr_callback(unsigned char *iq_buf, uint32_t len, void *ctx)
{
    r_cfg_t *cfg = ctx;
    struct dm_state *demod = cfg->demod;
    char time_str[LOCAL_TIME_BUFLEN];
    unsigned long n_samples;

    for (size_t i = 0; i < cfg->output_handler.len; ++i) { // list might contain NULLs
        data_output_poll(cfg->output_handler.elems[i]);
    }

    if (cfg->do_exit || cfg->do_exit_async)
        return;

    if ((cfg->bytes_to_read > 0) && (cfg->bytes_to_read <= len)) {
        len = cfg->bytes_to_read;
        cfg->do_exit = 1;
        sdr_stop(cfg->dev);
    }

    get_time_now(&demod->now);

    n_samples = len / 2 / demod->sample_size;

    // age the frame position if there is one
    if (demod->frame_start_ago)
        demod->frame_start_ago += n_samples;
    if (demod->frame_end_ago)
        demod->frame_end_ago += n_samples;

#ifndef _WIN32
    alarm(3); // require callback to run every 3 second, abort otherwise
#endif

    if (demod->samp_grab) {
        samp_grab_push(demod->samp_grab, iq_buf, len);
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
    if (demod->r_devs.len || demod->analyze_pulses || demod->dumper.len || demod->samp_grab) {
        // Detect a package and loop through demodulators with pulse data
        int package_type = 1;  // Just to get us started
        for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
            file_info_t const *dumper = *iter;
            if (dumper->format == U8_LOGIC) {
                memset(demod->u8_buf, 0, n_samples);
                break;
            }
        }
        while (package_type) {
            int p_events = 0; // Sensor events successfully detected per package
            package_type = pulse_detect_package(demod->pulse_detect, demod->am_buf, demod->buf.fm, n_samples, demod->level_limit, cfg->samp_rate, cfg->input_pos, &demod->pulse_data, &demod->fsk_pulse_data);
            if (package_type) {
                // new package: set a first frame start if we are not tracking one already
                if (!demod->frame_start_ago)
                    demod->frame_start_ago = demod->pulse_data.start_ago;
                // always update the last frame end
                demod->frame_end_ago = demod->pulse_data.end_ago;
            }
            if (package_type == 1) {
                calc_rssi_snr(cfg, &demod->pulse_data);
                if (demod->analyze_pulses) fprintf(stderr, "Detected OOK package\t%s\n", time_pos_str(cfg, demod->pulse_data.start_ago, time_str));

                p_events += run_ook_demods(&demod->r_devs, &demod->pulse_data);

                for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
                    file_info_t const *dumper = *iter;
                    if (dumper->format == VCD_LOGIC) pulse_data_print_vcd(dumper->file, &demod->pulse_data, '\'');
                    if (dumper->format == U8_LOGIC) pulse_data_dump_raw(demod->u8_buf, n_samples, cfg->input_pos, &demod->pulse_data, 0x02);
                    if (dumper->format == PULSE_OOK) pulse_data_dump(dumper->file, &demod->pulse_data);
                }

                if (cfg->verbosity > 2) pulse_data_print(&demod->pulse_data);
                if (demod->analyze_pulses && (cfg->grab_mode <= 1 || (cfg->grab_mode == 2 && p_events == 0) || (cfg->grab_mode == 3 && p_events > 0)) ) {
                    pulse_analyzer(&demod->pulse_data);
                }

            } else if (package_type == 2) {
                calc_rssi_snr(cfg, &demod->fsk_pulse_data);
                if (demod->analyze_pulses) fprintf(stderr, "Detected FSK package\t%s\n", time_pos_str(cfg, demod->fsk_pulse_data.start_ago, time_str));

                p_events += run_fsk_demods(&demod->r_devs, &demod->fsk_pulse_data);

                for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
                    file_info_t const *dumper = *iter;
                    if (dumper->format == VCD_LOGIC) pulse_data_print_vcd(dumper->file, &demod->fsk_pulse_data, '"');
                    if (dumper->format == U8_LOGIC) pulse_data_dump_raw(demod->u8_buf, n_samples, cfg->input_pos, &demod->fsk_pulse_data, 0x04);
                    if (dumper->format == PULSE_OOK) pulse_data_dump(dumper->file, &demod->fsk_pulse_data);
                }

                if (cfg->verbosity > 2) pulse_data_print(&demod->fsk_pulse_data);
                if (demod->analyze_pulses && (cfg->grab_mode <= 1 || (cfg->grab_mode == 2 && p_events == 0) || (cfg->grab_mode == 3 && p_events > 0)) ) {
                    pulse_analyzer(&demod->fsk_pulse_data);
                }
            } // if (package_type == ...
            d_events += p_events;
        } // while (package_type)...

        // add event counter to the frames currently tracked
        demod->frame_event_count += d_events;

        // end frame tracking if older than a whole buffer
        if (demod->frame_start_ago && demod->frame_end_ago > n_samples) {
            if (demod->samp_grab) {
                if (cfg->grab_mode == 1
                        || (cfg->grab_mode == 2 && demod->frame_event_count == 0)
                        || (cfg->grab_mode == 3 && demod->frame_event_count > 0)) {
                    unsigned frame_pad = n_samples / 8; // this could also be a fixed value, e.g. 10000 samples
                    unsigned start_padded = demod->frame_start_ago + frame_pad;
                    unsigned end_padded = demod->frame_end_ago - frame_pad;
                    unsigned len_padded = start_padded - end_padded;
                    samp_grab_write(demod->samp_grab, len_padded, end_padded);
                }
            }
            demod->frame_start_ago = 0;
            demod->frame_event_count = 0;
        }

        // dump partial pulse_data for this buffer
        for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
            file_info_t const *dumper = *iter;
            if (dumper->format == U8_LOGIC) {
                pulse_data_dump_raw(demod->u8_buf, n_samples, cfg->input_pos, &demod->pulse_data, 0x02);
                pulse_data_dump_raw(demod->u8_buf, n_samples, cfg->input_pos, &demod->fsk_pulse_data, 0x04);
                break;
            }
        }

        if (cfg->stop_after_successful_events_flag && (d_events > 0)) {
            cfg->do_exit = cfg->do_exit_async = 1;
            sdr_stop(cfg->dev);
        }
    }

    if (demod->am_analyze) {
        am_analyze(demod->am_analyze, demod->am_buf, n_samples, cfg->verbosity > 1, NULL);
    }

    for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
        file_info_t const *dumper = *iter;
        if (!dumper->file
                || dumper->format == VCD_LOGIC
                || dumper->format == PULSE_OOK)
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
        }
        else if (dumper->format == CS16_IQ) {
            if (demod->sample_size == 1) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((int16_t *)demod->buf.temp)[n] = (iq_buf[n] << 8) - 32768; // scale Q0.7 to Q0.15
                out_buf = (uint8_t *)demod->buf.temp; // this buffer is too small if out_block_size is large
                out_len = n_samples * 2 * sizeof(int16_t);
            }
        }
        else if (dumper->format == CS8_IQ) {
            if (demod->sample_size == 1) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((int8_t *)demod->buf.temp)[n] = (iq_buf[n] - 128);
            }
            else if (demod->sample_size == 2) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((int8_t *)demod->buf.temp)[n] = ((int16_t *)iq_buf)[n] >> 8;
            }
            out_buf = (uint8_t *)demod->buf.temp;
            out_len = n_samples * 2 * sizeof(int8_t);
        }
        else if (dumper->format == CF32_IQ) {
            if (demod->sample_size == 1) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((float *)demod->buf.temp)[n] = (iq_buf[n] - 128) / 128.0;
            }
            else if (demod->sample_size == 2) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((float *)demod->buf.temp)[n] = ((int16_t *)iq_buf)[n] / 32768.0;
            }
            out_buf = (uint8_t *)demod->buf.temp; // this buffer is too small if out_block_size is large
            out_len = n_samples * 2 * sizeof(float);
        }
        else if (dumper->format == S16_AM) {
            out_buf = (uint8_t *)demod->am_buf;
            out_len = n_samples * sizeof(int16_t);
        }
        else if (dumper->format == S16_FM) {
            out_buf = (uint8_t *)demod->buf.fm;
            out_len = n_samples * sizeof(int16_t);
        }
        else if (dumper->format == F32_AM) {
            for (unsigned long n = 0; n < n_samples; ++n)
                demod->f32_buf[n] = demod->am_buf[n] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == F32_FM) {
            for (unsigned long n = 0; n < n_samples; ++n)
                demod->f32_buf[n] = demod->buf.fm[n] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == F32_I) {
            if (demod->sample_size == 1)
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = (iq_buf[n * 2] - 128) * (1.0 / 0x80); // scale from Q0.7
            else
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = ((int16_t *)iq_buf)[n * 2] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == F32_Q) {
            if (demod->sample_size == 1)
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = (iq_buf[n * 2 + 1] - 128) * (1.0 / 0x80); // scale from Q0.7
            else
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = ((int16_t *)iq_buf)[n * 2 + 1] * (1.0 / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == U8_LOGIC) { // state data
            out_buf = demod->u8_buf;
            out_len = n_samples;
        }

        if (fwrite(out_buf, 1, out_len, dumper->file) != out_len) {
            fprintf(stderr, "Short write, samples lost, exiting!\n");
            sdr_stop(cfg->dev);
        }
    }

    cfg->input_pos += n_samples;
    if (cfg->bytes_to_read > 0)
        cfg->bytes_to_read -= len;

    time_t rawtime;
    time(&rawtime);
    if (cfg->frequencies > 1 && difftime(rawtime, cfg->rawtime_old) > demod->hop_time) {
        cfg->rawtime_old = rawtime;
        cfg->do_exit_async = 1;
#ifndef _WIN32
        alarm(0); // cancel the watchdog timer
#endif
        sdr_stop(cfg->dev);
    }
    if (cfg->duration > 0 && rawtime >= cfg->stop_time) {
        cfg->do_exit_async = cfg->do_exit = 1;
#ifndef _WIN32
        alarm(0); // cancel the watchdog timer
#endif
        sdr_stop(cfg->dev);
        fprintf(stderr, "Time expired, exiting!\n");
    }
}

// find the fields output for CSV
static char const **determine_csv_fields(r_cfg_t *cfg, char const **well_known, int *num_fields)
{
    list_t field_list = {0};
    list_ensure_size(&field_list, 100);

    // always add well-known fields
    list_push_all(&field_list, (void **)well_known);

    list_t *r_devs = &cfg->demod->r_devs;
    for (void **iter = r_devs->elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        if (!r_dev->disabled) {
            if (r_dev->fields)
                list_push_all(&field_list, (void **)r_dev->fields);
            else
                fprintf(stderr, "rtl_433: warning: %d \"%s\" does not support CSV output\n",
                        r_dev->protocol_num, r_dev->name);
        }
    }

    if (num_fields)
        *num_fields = field_list.len;
    return (char const **)field_list.elems;
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

static void add_json_output(r_cfg_t *cfg, char *param)
{
    list_push(&cfg->output_handler, data_output_json_create(fopen_output(param)));
}

static void add_csv_output(r_cfg_t *cfg, char *param)
{
    list_push(&cfg->output_handler, data_output_csv_create(fopen_output(param)));
}

static void start_outputs(r_cfg_t *cfg, char const **well_known)
{
    int num_output_fields;
    const char **output_fields = determine_csv_fields(cfg, well_known, &num_output_fields);

    for (size_t i = 0; i < cfg->output_handler.len; ++i) { // list might contain NULLs
        data_output_start(cfg->output_handler.elems[i], output_fields, num_output_fields);
    }

    free(output_fields);
}

static void add_kv_output(r_cfg_t *cfg, char *param)
{
    list_push(&cfg->output_handler, data_output_kv_create(fopen_output(param)));
}

static void add_syslog_output(r_cfg_t *cfg, char *param)
{
    char *host = "localhost";
    char *port = "514";
    hostport_param(param, &host, &port);
    fprintf(stderr, "Syslog UDP datagrams to %s port %s\n", host, port);

    list_push(&cfg->output_handler, data_output_syslog_create(host, port));
}

static void add_null_output(r_cfg_t *cfg, char *param)
{
    list_push(&cfg->output_handler, NULL);
}

static void add_dumper(r_cfg_t *cfg, char const *spec, int overwrite)
{
    file_info_t *dumper = calloc(1, sizeof(*dumper));
    list_push(&cfg->demod->dumper, dumper);

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
        pulse_data_print_vcd_header(dumper->file, cfg->samp_rate);
    }
    if (dumper->format == PULSE_OOK) {
        pulse_data_print_pulse_header(dumper->file);
    }
}

static void add_infile(r_cfg_t *cfg, char *in_file)
{
    list_push(&cfg->in_files, in_file);
}

static int hasopt(int test, int argc, char *argv[], char const *optstring)
{
    int opt;

    optind = 1; // reset getopt
    while ((opt = getopt(argc, argv, optstring)) != -1) {
        if (opt == test || optopt == test)
            return opt;
    }
    return 0;
}

static void parse_conf_option(r_cfg_t *cfg, int opt, char *arg);

#define OPTSTRING "hVvqDc:x:z:p:aAI:S:m:M:r:w:W:l:d:t:f:H:g:s:b:n:R:X:F:K:C:T:UGy:E"

// these should match the short options exactly
static struct conf_keywords const conf_keywords[] = {
        {"help", 'h'},
        {"verbose", 'v'},
        {"version", 'V'},
        {"config_file", 'c'},
        {"report_meta", 'M'},
        {"device", 'd'},
        {"settings", 't'},
        {"gain", 'g'},
        {"frequency", 'f'},
        {"hop_interval", 'H'},
        {"ppm_error", 'p'},
        {"sample_rate", 's'},
        {"protocol", 'R'},
        {"decoder", 'X'},
        {"register_all", 'G'},
        {"out_block_size", 'b'},
        {"level_limit", 'l'},
        {"samples_to_read", 'n'},
        {"analyze", 'a'},
        {"analyze_pulses", 'A'},
        {"include_only", 'I'},
        {"read_file", 'r'},
        {"write_file", 'w'},
        {"overwrite_file", 'W'},
        {"signal_grabber", 'S'},
        {"override_short", 'z'},
        {"override_long", 'x'},
        {"output", 'F'},
        {"output_tag", 'K'},
        {"convert", 'C'},
        {"duration", 'T'},
        {"test_data", 'y'},
        {"stop_after_successful_events", 'E'},
        {NULL, 0}};

static void parse_conf_text(r_cfg_t *cfg, char *conf)
{
    int opt;
    char *arg;
    char *p = conf;

    if (!conf || !*conf)
        return;

    while ((opt = getconf(&p, conf_keywords, &arg)) != -1) {
        parse_conf_option(cfg, opt, arg);
    }
}

static void parse_conf_file(r_cfg_t *cfg, char const *path)
{
    if (!path || !*path || !strcmp(path, "null") || !strcmp(path, "0"))
        return;

    char *conf = readconf(path);
    parse_conf_text(cfg, conf);
    //free(conf); // TODO: check no args are dangling, then use free
}

static void parse_conf_try_default_files(r_cfg_t *cfg)
{
    char **paths = compat_get_default_conf_paths();
    for (int a = 0; paths[a]; a++) {
        fprintf(stderr, "Trying conf file at \"%s\"...\n", paths[a]);
        if (hasconf(paths[a])) {
            fprintf(stderr, "Reading conf from \"%s\".\n", paths[a]);
            parse_conf_file(cfg, paths[a]);
            break;
        }
    }
}

static void parse_conf_args(r_cfg_t *cfg, int argc, char *argv[])
{
    int opt;

    optind = 1; // reset getopt
    while ((opt = getopt(argc, argv, OPTSTRING)) != -1) {
        if (opt == '?')
            opt = optopt; // allow missing arguments
        parse_conf_option(cfg, opt, optarg);
    }
}

static void parse_conf_option(r_cfg_t *cfg, int opt, char *arg)
{
    unsigned i;
    int n;
    r_device *flex_device;

    if (arg && (!strcmp(arg, "help") || !strcmp(arg, "?"))) {
        arg = NULL; // remove the arg if it's a request for the usage help
    }

    switch (opt) {
    case 'h':
        usage(0);
        break;
    case 'V':
        exit(0); // we already printed the version
        break;
    case 'v':
        if (!arg)
            cfg->verbosity++;
        else
            cfg->verbosity = atobv(arg, 1);
        break;
    case 'c':
        parse_conf_file(cfg, arg);
        break;
    case 'd':
        if (!arg)
            help_device();

        cfg->dev_query = arg;
        break;
    case 't':
        // this option changed, check and warn if old meaning is used
        if (!arg || *arg == '-') {
            fprintf(stderr, "test_mode (-t) is deprecated. Use -S none|all|unknown|known\n");
            exit(1);
        }
        cfg->settings_str = arg;
        break;
    case 'f':
        if (cfg->frequencies < MAX_FREQS)
            cfg->frequency[cfg->frequencies++] = atouint32_metric(arg, "-f: ");
        else
            fprintf(stderr, "Max number of frequencies reached %d\n", MAX_FREQS);
        break;
    case 'H':
        cfg->demod->hop_time = atoi_time(arg, "-H: ");
        break;
    case 'g':
        if (!arg)
            help_gain();

        cfg->gain_str = arg;
        break;
    case 'G':
        if (atobv(arg, 1)) {
            cfg->no_default_devices = 1;
            register_all_protocols(cfg, 1);
        }
        break;
    case 'p':
        cfg->ppm_error = atobv(arg, 0);
        break;
    case 's':
        cfg->samp_rate = atouint32_metric(arg, "-s: ");
        break;
    case 'b':
        cfg->out_block_size = atouint32_metric(arg, "-b: ");
        break;
    case 'l':
        cfg->demod->level_limit = atouint32_metric(arg, "-l: ");
        break;
    case 'n':
        cfg->bytes_to_read = atouint32_metric(arg, "-n: ") * 2;
        break;
    case 'a':
        if (atobv(arg, 1) && !cfg->demod->am_analyze)
            cfg->demod->am_analyze = am_analyze_create();
        break;
    case 'A':
        cfg->demod->analyze_pulses = atobv(arg, 1);
        break;
    case 'I':
        fprintf(stderr, "include_only (-I) is deprecated. Use -S none|all|unknown|known\n");
        exit(1);
        break;
    case 'r':
        if (!arg)
            help_read();

        add_infile(cfg, arg);
        // TODO: check_read_file_info()
        break;
    case 'w':
        if (!arg)
            help_write();

        add_dumper(cfg, arg, 0);
        break;
    case 'W':
        if (!arg)
            help_write();

        add_dumper(cfg, arg, 1);
        break;
    case 'S':
        if (strcasecmp(arg, "all") == 0)
            cfg->grab_mode = 1;
        else if (strcasecmp(arg, "unknown") == 0)
            cfg->grab_mode = 2;
        else if (strcasecmp(arg, "known") == 0)
            cfg->grab_mode = 3;
        else
            cfg->grab_mode = atobv(arg, 1);
        if (cfg->grab_mode && !cfg->demod->samp_grab)
            cfg->demod->samp_grab = samp_grab_create(SIGNAL_GRABBER_BUFFER);
        break;
    case 'm':
        fprintf(stderr, "sample mode option is deprecated.\n");
        usage(1);
        break;
    case 'M':
        if (!arg)
            help_meta();

        if (!strcasecmp(arg, "time"))
            cfg->report_time = REPORT_TIME_DATE;
        else if (!strcasecmp(arg, "reltime"))
            cfg->report_time = REPORT_TIME_SAMPLES;
        else if (!strcasecmp(arg, "notime"))
            cfg->report_time = REPORT_TIME_OFF;
        else if (!strcasecmp(arg, "hires"))
            cfg->report_time_hires = 1;
        else if (!strcasecmp(arg, "utc"))
            cfg->report_time_utc = 1;
        else if (!strcasecmp(arg, "noutc"))
            cfg->report_time_utc = 0;
        else if (!strcasecmp(arg, "protocol"))
            cfg->report_protocol = 1;
        else if (!strcasecmp(arg, "noprotocol"))
            cfg->report_protocol = 0;
        else if (!strcasecmp(arg, "level"))
            cfg->report_meta = 1;
        else if (!strcasecmp(arg, "bits"))
            cfg->verbose_bits = 1;
        else if (!strcasecmp(arg, "description"))
            cfg->report_description = 1;
        else if (!strcasecmp(arg, "newmodel"))
            cfg->new_model_keys = 1;
        else if (!strcasecmp(arg, "oldmodel"))
            cfg->new_model_keys = 0;
        else
            cfg->report_meta = atobv(arg, 1);
        break;
    case 'D':
        fprintf(stderr, "debug option (-D) is deprecated. See -v to increase verbosity\n");
        break;
    case 'z':
        if (cfg->demod->am_analyze)
            cfg->demod->am_analyze->override_short = atoi(arg);
        break;
    case 'x':
        if (cfg->demod->am_analyze)
            cfg->demod->am_analyze->override_long = atoi(arg);
        break;
    case 'R':
        if (!arg)
            help_protocols(cfg->devices, cfg->num_r_devices, 0);

        n = atoi(arg);
        if (n > cfg->num_r_devices || -n > cfg->num_r_devices) {
            fprintf(stderr, "Protocol number specified (%d) is larger than number of protocols\n\n", n);
            help_protocols(cfg->devices, cfg->num_r_devices, 1);
        }
        if ((n > 0 && cfg->devices[n - 1].disabled > 2) || (n < 0 && cfg->devices[-n - 1].disabled > 2)) {
            fprintf(stderr, "Protocol number specified (%d) is invalid\n\n", n);
            help_protocols(cfg->devices, cfg->num_r_devices, 1);
        }

        if (n < 0 && !cfg->no_default_devices) {
            register_all_protocols(cfg, 0); // register all defaults
        }
        cfg->no_default_devices = 1;

        if (n >= 1) {
            register_protocol(cfg, &cfg->devices[n - 1], arg_param(arg));
        }
        else if (n <= -1) {
            unregister_protocol(cfg, &cfg->devices[-n - 1]);
        }
        else {
            fprintf(stderr, "Disabling all device decoders.\n");
            list_clear(&cfg->demod->r_devs, (list_elem_free_fn)free_protocol);
        }
        break;
    case 'X':
        if (!arg)
            flex_create_device(NULL);

        flex_device = flex_create_device(arg);
        register_protocol(cfg, flex_device, "");
        break;
    case 'q':
        fprintf(stderr, "quiet option (-q) is default and deprecated. See -v to increase verbosity\n");
        break;
    case 'F':
        if (!arg)
            help_output();

        if (strncmp(arg, "json", 4) == 0) {
            add_json_output(cfg, arg_param(arg));
        }
        else if (strncmp(arg, "csv", 3) == 0) {
            add_csv_output(cfg, arg_param(arg));
        }
        else if (strncmp(arg, "kv", 2) == 0) {
            add_kv_output(cfg, arg_param(arg));
        }
        else if (strncmp(arg, "syslog", 6) == 0) {
            add_syslog_output(cfg, arg_param(arg));
        }
        else if (strncmp(arg, "null", 4) == 0) {
            add_null_output(cfg, arg_param(arg));
        }
        else {
            fprintf(stderr, "Invalid output format %s\n", arg);
            usage(1);
        }
        break;
    case 'K':
        cfg->output_tag = arg;
        break;
    case 'C':
        if (strcmp(arg, "native") == 0) {
            cfg->conversion_mode = CONVERT_NATIVE;
        }
        else if (strcmp(arg, "si") == 0) {
            cfg->conversion_mode = CONVERT_SI;
        }
        else if (strcmp(arg, "customary") == 0) {
            cfg->conversion_mode = CONVERT_CUSTOMARY;
        }
        else {
            fprintf(stderr, "Invalid conversion mode %s\n", arg);
            usage(1);
        }
        break;
    case 'U':
        fprintf(stderr, "UTC mode option (-U) is deprecated. Please use \"-M utc\".\n");
        exit(1);
        break;
    case 'T':
        cfg->duration = atoi_time(arg, "-T: ");
        if (cfg->duration < 1) {
            fprintf(stderr, "Duration '%s' not a positive number; will continue indefinitely\n", arg);
        }
        break;
    case 'y':
        cfg->test_data = arg;
        break;
    case 'E':
        cfg->stop_after_successful_events_flag = atobv(arg, 1);
        break;
    default:
        usage(1);
        break;
    }
}

void r_init_cfg(r_cfg_t *cfg)
{
    cfg->out_block_size  = DEFAULT_BUF_LENGTH;
    cfg->samp_rate       = DEFAULT_SAMPLE_RATE;
    cfg->conversion_mode = CONVERT_NATIVE;

    list_ensure_size(&cfg->in_files, 100);
    list_ensure_size(&cfg->output_handler, 16);

    cfg->demod = calloc(1, sizeof(*cfg->demod));
    if (!cfg->demod) {
        fprintf(stderr, "Could not create demod!\n");
        exit(1);
    }

    cfg->demod->level_limit = DEFAULT_LEVEL_LIMIT;
    cfg->demod->hop_time    = DEFAULT_HOP_TIME;

    list_ensure_size(&cfg->demod->r_devs, 100);
    list_ensure_size(&cfg->demod->dumper, 32);
}

r_cfg_t *r_create_cfg(void)
{
    r_cfg_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        fprintf(stderr, "Could not create cfg!\n");
        exit(1);
    }

    r_init_cfg(cfg);

    return cfg;
}

void r_free_cfg(r_cfg_t *cfg)
{
    if (cfg->dev)
        sdr_deactivate(cfg->dev);
    if (cfg->dev)
        sdr_close(cfg->dev);

    for (void **iter = cfg->demod->dumper.elems; iter && *iter; ++iter) {
        file_info_t const *dumper = *iter;
        if (dumper->file && (dumper->file != stdout))
            fclose(dumper->file);
    }
    list_free_elems(&cfg->demod->dumper, free);

    list_free_elems(&cfg->demod->r_devs, free);

    if (cfg->demod->am_analyze)
        am_analyze_free(cfg->demod->am_analyze);

    pulse_detect_free(cfg->demod->pulse_detect);

    free(cfg->demod);

    list_free_elems(&cfg->output_handler, (list_elem_free_fn)data_output_free);

    list_free_elems(&cfg->in_files, NULL);

    //free(cfg);
}

// well-known fields "time", "msg" and "codes" are used to output general decoder messages
// well-known field "bits" is only used when verbose bits (-M bits) is requested
// well-known field "tag" is only used when output tagging is requested
// well-known field "protocol" is only used when model protocol is requested
// well-known field "description" is only used when model description is requested
// well-known fields "mod", "freq", "freq1", "freq2", "rssi", "snr", "noise" are used by meta report option
static char const *well_known_default[15] = {0};
static char const **well_known_output_fields(r_cfg_t *cfg)
{
    char const **p = well_known_default;
    *p++ = "time";
    *p++ = "msg";
    *p++ = "codes";

    if (cfg->verbose_bits)
        *p++ = "bits";
    if (cfg->output_tag)
        *p++ = "tag";
    if (cfg->report_protocol)
        *p++ = "protocol";
    if (cfg->report_description)
        *p++ = "description";
    if (cfg->report_meta) {
        *p++ = "mod";
        *p++ = "freq";
        *p++ = "freq1";
        *p++ = "freq2";
        *p++ = "rssi";
        *p++ = "snr";
        *p++ = "noise";
    }

    return well_known_default;
}

static r_cfg_t cfg;

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
    if (CTRL_C_EVENT == signum) {
        fprintf(stderr, "Signal caught, exiting!\n");
        cfg.do_exit = 1;
        sdr_stop(cfg.dev);
        return TRUE;
    }
    return FALSE;
}
#else
static void sighandler(int signum)
{
    if (signum == SIGPIPE) {
        signal(SIGPIPE, SIG_IGN);
    }
    else if (signum == SIGALRM) {
        fprintf(stderr, "Async read stalled, exiting!\n");
    }
    else {
        fprintf(stderr, "Signal caught, exiting!\n");
    }
    cfg.do_exit = 1;
    sdr_stop(cfg.dev);
}
#endif

int main(int argc, char **argv) {
#ifndef _WIN32
    struct sigaction sigact;
#endif
    FILE *in_file;
    int r = 0;
    unsigned i;
    struct dm_state *demod;

    print_version(); // always print the version info

    r_init_cfg(&cfg);

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    demod = cfg.demod;

    demod->pulse_detect = pulse_detect_create();

    /* initialize tables */
    baseband_init();

    r_device r_devices[] = {
#define DECL(name) name,
            DEVICES
#undef DECL
            };

    cfg.num_r_devices = sizeof(r_devices) / sizeof(*r_devices);
    for (i = 0; i < cfg.num_r_devices; i++) {
        r_devices[i].protocol_num = i + 1;
    }
    cfg.devices = r_devices;

    // if there is no explicit conf file option look for default conf files
    if (!hasopt('c', argc, argv, OPTSTRING)) {
        parse_conf_try_default_files(&cfg);
    }

    parse_conf_args(&cfg, argc, argv);

    // warn if still using old model keys
    if (!cfg.new_model_keys) {
        fprintf(stderr,
                "\n\tConsider using \"-M newmodel\" to transition to new model keys. This will become the default someday.\n"
                "\tA table of changes and discussion is at https://github.com/merbanan/rtl_433/pull/986.\n\n");
    }

    // add all remaining positional arguments as input files
    while (argc > optind) {
        add_infile(&cfg, argv[optind++]);
    }

    if (demod->am_analyze) {
        demod->am_analyze->level_limit = &demod->level_limit;
        demod->am_analyze->frequency   = &cfg.center_frequency;
        demod->am_analyze->samp_rate   = &cfg.samp_rate;
        demod->am_analyze->sample_size = &demod->sample_size;
    }

    if (demod->samp_grab) {
        demod->samp_grab->frequency   = &cfg.center_frequency;
        demod->samp_grab->samp_rate   = &cfg.samp_rate;
        demod->samp_grab->sample_size = &demod->sample_size;
    }

    if (cfg.report_time == REPORT_TIME_DEFAULT) {
        if (cfg.in_files.len)
            cfg.report_time = REPORT_TIME_SAMPLES;
        else
            cfg.report_time = REPORT_TIME_DATE;
    }
    if (cfg.report_time_utc) {
#ifdef _WIN32
        putenv("TZ=UTC+0");
        _tzset();
#else
        r = setenv("TZ", "UTC", 1);
        if (r != 0)
            fprintf(stderr, "Unable to set TZ to UTC; error code: %d\n", r);
#endif
    }

    if (!cfg.output_handler.len) {
        add_kv_output(&cfg, NULL);
    }

    // register default decoders if nothing is configured
    if (!cfg.no_default_devices) {
        register_all_protocols(&cfg, 0); // register all defaults
    }

    // check if we need FM demod
    for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        if (r_dev->modulation >= FSK_DEMOD_MIN_VAL) {
          demod->enable_FM_demod = 1;
          break;
        }
    }

    fprintf(stderr, "Registered %zu out of %d device decoding protocols",
            demod->r_devs.len, cfg.num_r_devices);

    if (!cfg.verbosity) {
        // print registered decoder ranges
        fprintf(stderr, " [");
        for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
            r_device *r_dev = *iter;
            unsigned num = r_dev->protocol_num;
            if (num == 0)
                continue;
            while (iter[1]
                    && r_dev->protocol_num + 1 == ((r_device *)iter[1])->protocol_num)
                r_dev = *++iter;
            if (num == r_dev->protocol_num)
                fprintf(stderr, " %d", num);
            else
                fprintf(stderr, " %d-%d", num, r_dev->protocol_num);
        }
        fprintf(stderr, " ]");
    }
    fprintf(stderr, "\n");

    start_outputs(&cfg, well_known_output_fields(&cfg));

    if (cfg.out_block_size < MINIMAL_BUF_LENGTH ||
            cfg.out_block_size > MAXIMAL_BUF_LENGTH) {
        fprintf(stderr,
                "Output block size wrong value, falling back to default\n");
        fprintf(stderr,
                "Minimal length: %u\n", MINIMAL_BUF_LENGTH);
        fprintf(stderr,
                "Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
        cfg.out_block_size = DEFAULT_BUF_LENGTH;
    }

    // Special case for test data
    if (cfg.test_data) {
        r = 0;
        for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
            r_device *r_dev = *iter;
            if (cfg.verbosity)
                fprintf(stderr, "Verifying test data with device %s.\n", r_dev->name);
            r += pulse_demod_string(cfg.test_data, r_dev);
        }
        r_free_cfg(&cfg);
        exit(!r);
    }

    // Special case for in files
    if (cfg.in_files.len) {
        unsigned char *test_mode_buf = malloc(DEFAULT_BUF_LENGTH * sizeof(unsigned char));
        float *test_mode_float_buf = malloc(DEFAULT_BUF_LENGTH / sizeof(int16_t) * sizeof(float));
        if (!test_mode_buf || !test_mode_float_buf)
        {
            fprintf(stderr, "Couldn't allocate read buffers!\n");
            exit(1);
        }

        if (cfg.duration > 0) {
            time(&cfg.stop_time);
            cfg.stop_time += cfg.duration;
        }

        for (void **iter = cfg.in_files.elems; iter && *iter; ++iter) {
            cfg.in_filename = *iter;

            parse_file_info(cfg.in_filename, &demod->load_info);
            if (strcmp(demod->load_info.path, "-") == 0) { /* read samples from stdin */
                in_file = stdin;
                cfg.in_filename = "<stdin>";
            } else {
                in_file = fopen(demod->load_info.path, "rb");
                if (!in_file) {
                    fprintf(stderr, "Opening file: %s failed!\n", cfg.in_filename);
                    break;
                }
            }
            fprintf(stderr, "Test mode active. Reading samples from file: %s\n", cfg.in_filename);  // Essential information (not quiet)
            if (demod->load_info.format == CU8_IQ
                    || demod->load_info.format == S16_AM
                    || demod->load_info.format == S16_FM) {
                demod->sample_size = sizeof(uint8_t); // CU8, AM, FM
            } else {
                demod->sample_size = sizeof(int16_t); // CF32, CS16
            }
            if (cfg.verbosity) {
                fprintf(stderr, "Input format: %s\n", file_info_string(&demod->load_info));
            }
            demod->sample_file_pos = 0.0;

            // special case for pulse data file-inputs
            if (demod->load_info.format == PULSE_OOK) {
                while (!cfg.do_exit) {
                    pulse_data_load(in_file, &demod->pulse_data);
                    if (!demod->pulse_data.num_pulses)
                        break;

                    if (demod->pulse_data.fsk_f2_est) {
                        run_fsk_demods(&demod->r_devs, &demod->pulse_data);
                    }
                    else {
                        run_ook_demods(&demod->r_devs, &demod->pulse_data);
                    }
                }

                if (in_file != stdin)
                    fclose(in_file = stdin);

                continue;
            }

            // default case for file-inputs
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
                        ((int16_t *)test_mode_buf)[n] = s_tmp;
                    }
                    n_read *= 2; // convert to byte count
                } else {
                    n_read = fread(test_mode_buf, 1, DEFAULT_BUF_LENGTH, in_file);
                }
                if (n_read == 0) break;  // sdr_callback() will Segmentation Fault with len=0
                demod->sample_file_pos = ((float)n_blocks * DEFAULT_BUF_LENGTH + n_read) / cfg.samp_rate / 2 / demod->sample_size;
                n_blocks++; // this assumes n_read == DEFAULT_BUF_LENGTH
                sdr_callback(test_mode_buf, n_read, &cfg);
            } while (n_read != 0 && !cfg.do_exit);

            // Call a last time with cleared samples to ensure EOP detection
            if (demod->sample_size == 1) { // CU8
                memset(test_mode_buf, 128, DEFAULT_BUF_LENGTH); // 128 is 0 in unsigned data
                // or is 127.5 a better 0 in cu8 data?
                //for (unsigned long n = 0; n < DEFAULT_BUF_LENGTH/2; n++)
                //    ((uint16_t *)test_mode_buf)[n] = 0x807f;
            }
            else { // CF32, CS16
                    memset(test_mode_buf, 0, DEFAULT_BUF_LENGTH);
            }
            demod->sample_file_pos = ((float)n_blocks + 1) * DEFAULT_BUF_LENGTH / cfg.samp_rate / 2 / demod->sample_size;
            sdr_callback(test_mode_buf, DEFAULT_BUF_LENGTH, &cfg);

            //Always classify a signal at the end of the file
            if (demod->am_analyze)
                am_analyze_classify(demod->am_analyze);
            if (cfg.verbosity) {
                fprintf(stderr, "Test mode file issued %d packets\n", n_blocks);
            }

            if (in_file != stdin)
                fclose(in_file = stdin);
        }

        free(test_mode_buf);
        free(test_mode_float_buf);
        r_free_cfg(&cfg);
        exit(0);
    }

    // Normal case, no test data, no in files
    r = sdr_open(&cfg.dev, &demod->sample_size, cfg.dev_query, cfg.verbosity);
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
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)sighandler, TRUE);
#endif
    /* Set the sample rate */
    r = sdr_set_sample_rate(cfg.dev, cfg.samp_rate, 1); // always verbose

    if (cfg.verbosity || demod->level_limit)
        fprintf(stderr, "Bit detection level set to %d%s.\n", demod->level_limit, (demod->level_limit ? "" : " (Auto)"));

    r = sdr_apply_settings(cfg.dev, cfg.settings_str, 1); // always verbose for soapy

    /* Enable automatic gain if gain_str empty (or 0 for RTL-SDR), set manual gain otherwise */
    r = sdr_set_tuner_gain(cfg.dev, cfg.gain_str, 1); // always verbose

    if (cfg.ppm_error)
        r = sdr_set_freq_correction(cfg.dev, cfg.ppm_error, 1); // always verbose

    /* Reset endpoint before we start reading from it (mandatory) */
    r = sdr_reset(cfg.dev, cfg.verbosity);
    if (r < 0)
        fprintf(stderr, "WARNING: Failed to reset buffers.\n");
    r = sdr_activate(cfg.dev);

    if (cfg.frequencies == 0) {
        cfg.frequency[0] = DEFAULT_FREQUENCY;
        cfg.frequencies = 1;
    } else {
        time(&cfg.rawtime_old);
    }
    if (cfg.verbosity) {
        fprintf(stderr, "Reading samples in async mode...\n");
    }
    if (cfg.duration > 0) {
        time(&cfg.stop_time);
        cfg.stop_time += cfg.duration;
    }

    uint32_t samp_rate = cfg.samp_rate;
    while (!cfg.do_exit) {
        /* Set the cfg.frequency */
        cfg.center_frequency = cfg.frequency[cfg.frequency_index];
        r = sdr_set_center_freq(cfg.dev, cfg.center_frequency, 1); // always verbose

        if (samp_rate != cfg.samp_rate) {
            r = sdr_set_sample_rate(cfg.dev, cfg.samp_rate, 1); // always verbose
            update_protocols(&cfg);
            samp_rate = cfg.samp_rate;
        }

#ifndef _WIN32
        signal(SIGALRM, sighandler);
        alarm(3); // require callback to run every 3 second, abort otherwise
#endif
        r = sdr_start(cfg.dev, sdr_callback, (void *)&cfg,
                DEFAULT_ASYNC_BUF_NUMBER, cfg.out_block_size);
        if (r < 0) {
            fprintf(stderr, "WARNING: async read failed (%i).\n", r);
            break;
        }
#ifndef _WIN32
        alarm(0); // cancel the watchdog timer
#endif
        cfg.do_exit_async = 0;
        cfg.frequency_index = (cfg.frequency_index + 1) % cfg.frequencies;
    }

    if (!cfg.do_exit)
        fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

    r_free_cfg(&cfg);

    return r >= 0 ? r : -r;
}
