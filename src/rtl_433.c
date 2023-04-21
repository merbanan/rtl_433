/** @file
    rtl_433, turns your Realtek RTL2832 based DVB dongle into a 433.92MHz generic data receiver.

    Copyright (C) 2012 by Benjamin Larsson <benjamin@southpole.se>

    Based on rtl_sdr
    Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
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
#include <errno.h>
#include <signal.h>

#include "rtl_433.h"
#include "r_private.h"
#include "r_device.h"
#include "r_api.h"
#include "sdr.h"
#include "baseband.h"
#include "pulse_analyzer.h"
#include "pulse_detect.h"
#include "pulse_detect_fsk.h"
#include "pulse_slicer.h"
#include "rfraw.h"
#include "data.h"
#include "raw_output.h"
#include "r_util.h"
#include "optparse.h"
#include "abuf.h"
#include "fileformat.h"
#include "samp_grab.h"
#include "am_analyze.h"
#include "confparse.h"
#include "term_ctl.h"
#include "compat_paths.h"
#include "logger.h"
#include "fatal.h"
#include "write_sigrok.h"
#include "mongoose.h"

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

// note that Clang has _Noreturn but it's C11
// #if defined(__clang__) ...
#if !defined _Noreturn
#if defined(__GNUC__)
#define _Noreturn __attribute__((noreturn))
#elif defined(_MSC_VER)
#define _Noreturn __declspec(noreturn)
#else
#define _Noreturn
#endif
#endif

// STDERR_FILENO is not defined in at least MSVC
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifdef _WIN32
#include <windows.h>
#define usleep(us) Sleep((us) / 1000)
#endif

typedef struct timeval delay_timer_t;

static void delay_timer_init(delay_timer_t *delay_timer)
{
    // set to current wall clock
    get_time_now(delay_timer);
}

static void delay_timer_wait(delay_timer_t *delay_timer, unsigned delay_us)
{
    // sync to wall clock
    struct timeval now_tv;
    get_time_now(&now_tv);

    time_t elapsed_s  = now_tv.tv_sec - delay_timer->tv_sec;
    time_t elapsed_us = 1000000 * elapsed_s + now_tv.tv_usec - delay_timer->tv_usec;

    // set next wanted start time
    delay_timer->tv_usec += delay_us;
    while (delay_timer->tv_usec > 1000000) {
        delay_timer->tv_usec -= 1000000;
        delay_timer->tv_sec += 1;
    }

    if ((time_t)delay_us > elapsed_us)
        usleep(delay_us - elapsed_us);
}

r_device *flex_create_device(char *spec); // maybe put this in some header file?

static void print_version(void)
{
    fprintf(stderr, "%s\n", version_string());
    fprintf(stderr, "Use -h for usage help and see https://triq.org/ for documentation.\n");
}

_Noreturn
static void usage(int exit_code)
{
    term_help_printf(
            "Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.\n"
            "\nUsage:\n"
            "\t\t= General options =\n"
            "  [-V] Output the version string and exit\n"
            "  [-v] Increase verbosity (can be used multiple times).\n"
            "       -v : verbose notice, -vv : verbose info, -vvv : debug, -vvvv : trace.\n"
            "  [-c <path>] Read config options from a file\n"
            "\t\t= Tuner options =\n"
            "  [-d <RTL-SDR USB device index> | :<RTL-SDR USB device serial> | <SoapySDR device query> | rtl_tcp | help]\n"
            "  [-g <gain> | help] (default: auto)\n"
            "  [-t <settings>] apply a list of keyword=value settings to the SDR device\n"
            "       e.g. for SoapySDR -t \"antenna=A,bandwidth=4.5M,rfnotch_ctrl=false\"\n"
            "       for RTL-SDR use \"direct_samp[=1]\", \"offset_tune[=1]\", \"digital_agc[=1]\", \"biastee[=1]\"\n"
            "  [-f <frequency>] Receive frequency(s) (default: %i Hz)\n"
            "  [-H <seconds>] Hop interval for polling of multiple frequencies (default: %i seconds)\n"
            "  [-p <ppm_error>] Correct rtl-sdr tuner frequency offset error (default: 0)\n"
            "  [-s <sample rate>] Set sample rate (default: %i Hz)\n"
            "  [-D restart | pause | quit | manual] Input device run mode options.\n"
            "\t\t= Demodulator options =\n"
            "  [-R <device> | help] Enable only the specified device decoding protocol (can be used multiple times)\n"
            "       Specify a negative number to disable a device decoding protocol (can be used multiple times)\n"
            "  [-X <spec> | help] Add a general purpose decoder (prepend -R 0 to disable all decoders)\n"
            "  [-Y auto | classic | minmax] FSK pulse detector mode.\n"
            "  [-Y level=<dB level>] Manual detection level used to determine pulses (-1.0 to -30.0) (0=auto).\n"
            "  [-Y minlevel=<dB level>] Manual minimum detection level used to determine pulses (-1.0 to -99.0).\n"
            "  [-Y minsnr=<dB level>] Minimum SNR to determine pulses (1.0 to 99.0).\n"
            "  [-Y autolevel] Set minlevel automatically based on average estimated noise.\n"
            "  [-Y squelch] Skip frames below estimated noise level to reduce cpu load.\n"
            "  [-Y ampest | magest] Choose amplitude or magnitude level estimator.\n"
            "\t\t= Analyze/Debug options =\n"
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
            "  [-F log | kv | json | csv | mqtt | influx | syslog | trigger | null | help] Produce decoded output in given format.\n"
            "       Append output to file with :<filename> (e.g. -F csv:log.csv), defaults to stdout.\n"
            "       Specify host/port for syslog with e.g. -F syslog:127.0.0.1:1514\n"
            "  [-M time[:<options>] | protocol | level | noise[:<secs>] | stats | bits | help] Add various meta data to each output.\n"
            "  [-K FILE | PATH | <tag> | <key>=<tag>] Add an expanded token or fixed tag to every output line.\n"
            "  [-C native | si | customary] Convert units in decoded output.\n"
            "  [-n <value>] Specify number of samples to take (each sample is an I/Q pair)\n"
            "  [-T <seconds>] Specify number of seconds to run, also 12:34 or 1h23m45s\n"
            "  [-E hop | quit] Hop/Quit after outputting successful event(s)\n"
            "  [-h] Output this usage help and exit\n"
            "       Use -d, -g, -R, -X, -F, -M, -r, -w, or -W without argument for more help\n\n",
            DEFAULT_FREQUENCY, DEFAULT_HOP_TIME, DEFAULT_SAMPLE_RATE);
    exit(exit_code);
}

_Noreturn
static void help_protocols(r_device *devices, unsigned num_devices, int exit_code)
{
    unsigned i;
    char disabledc;

    if (devices) {
        term_help_printf("\t\t= Supported device protocols =\n");
        for (i = 0; i < num_devices; i++) {
            disabledc = devices[i].disabled ? '*' : ' ';
            if (devices[i].disabled <= 2) // if not hidden
                fprintf(stderr, "    [%02u]%c %s\n", i + 1, disabledc, devices[i].name);
        }
        fprintf(stderr, "\n* Disabled by default, use -R n or a conf file to enable\n");
    }
    exit(exit_code);
}

_Noreturn
static void help_device_selection(void)
{
    term_help_printf(
            "\t\t= Input device selection =\n"
#ifdef RTLSDR
            "\tRTL-SDR device driver is available.\n"
#else
            "\tRTL-SDR device driver is not available.\n"
#endif
            "  [-d <RTL-SDR USB device index>] (default: 0)\n"
            "  [-d :<RTL-SDR USB device serial (can be set with rtl_eeprom -s)>]\n"
            "\tTo set gain for RTL-SDR use -g <gain> to set an overall gain in dB.\n"
#ifdef SOAPYSDR
            "\tSoapySDR device driver is available.\n"
#else
            "\tSoapySDR device driver is not available.\n"
#endif
            "  [-d \"\"] Open default SoapySDR device\n"
            "  [-d driver=rtlsdr] Open e.g. specific SoapySDR device\n"
            "\tTo set gain for SoapySDR use -g ELEM=val,ELEM=val,... e.g. -g LNA=20,TIA=8,PGA=2 (for LimeSDR).\n"
            "  [-d rtl_tcp[:[//]host[:port]] (default: localhost:1234)\n"
            "\tSpecify host/port to connect to with e.g. -d rtl_tcp:127.0.0.1:1234\n");
    exit(0);
}

_Noreturn
static void help_gain(void)
{
    term_help_printf(
            "\t\t= Gain option =\n"
            "  [-g <gain>] (default: auto)\n"
            "\tFor RTL-SDR: gain in dB (\"0\" is auto).\n"
            "\tFor SoapySDR: gain in dB for automatic distribution (\"\" is auto), or string of gain elements.\n"
            "\tE.g. \"LNA=20,TIA=8,PGA=2\" for LimeSDR.\n");
    exit(0);
}

_Noreturn
static void help_device_mode(void)
{
    term_help_printf(
            "\t\t= Input device run mode =\n"
            "  [-D restart | pause | quit | manual] Input device run mode options.\n"
            "\tSupported input device run modes:\n"
            "\t  restart: Restart the input device on errors\n"
            "\t  pause: Pause the input device on errors, waits for e.g. HTTP-API control\n"
            "\t  quit: Quit on input device errors (default)\n"
            "\t  manual: Don't start an input device, waits for e.g. HTTP-API control\n"
            "\tWithout this option the default is to start the SDR and quit on errors.\n");
    exit(0);
}

_Noreturn
static void help_output(void)
{
    term_help_printf(
            "\t\t= Output format option =\n"
            "  [-F log|kv|json|csv|mqtt|influx|syslog|trigger|null] Produce decoded output in given format.\n"
            "\tWithout this option the default is LOG and KV output. Use \"-F null\" to remove the default.\n"
            "\tAppend output to file with :<filename> (e.g. -F csv:log.csv), defaults to stdout.\n"
            "\tSpecify MQTT server with e.g. -F mqtt://localhost:1883\n"
            "\tAdd MQTT options with e.g. -F \"mqtt://host:1883,opt=arg\"\n"
            "\tMQTT options are: user=foo, pass=bar, retain[=0|1], <format>[=topic]\n"
            "\tSupported MQTT formats: (default is all)\n"
            "\t  events: posts JSON event data\n"
            "\t  states: posts JSON state data\n"
            "\t  devices: posts device and sensor info in nested topics\n"
            "\tThe topic string will expand keys like [/model]\n"
            "\tE.g. -F \"mqtt://localhost:1883,user=USERNAME,pass=PASSWORD,retain=0,devices=rtl_433[/id]\"\n"
            "\tWith MQTT each rtl_433 instance needs a distinct driver selection. The MQTT Client-ID is computed from the driver string.\n"
            "\tIf you use multiple RTL-SDR, perhaps set a serial and select by that (helps not to get the wrong antenna).\n"
            "\tSpecify InfluxDB 2.0 server with e.g. -F \"influx://localhost:9999/api/v2/write?org=<org>&bucket=<bucket>,token=<authtoken>\"\n"
            "\tSpecify InfluxDB 1.x server with e.g. -F \"influx://localhost:8086/write?db=<db>&p=<password>&u=<user>\"\n"
            "\t  Additional parameter -M time:unix:usec:utc for correct timestamps in InfluxDB recommended\n"
            "\tSpecify host/port for syslog with e.g. -F syslog:127.0.0.1:1514\n");
    exit(0);
}

_Noreturn
static void help_tags(void)
{
    term_help_printf(
            "\t\t= Data tags option =\n"
            "  [-K FILE | PATH | <tag> | <key>=<tag>] Add an expanded token or fixed tag to every output line.\n"
            "\tIf <tag> is \"FILE\" or \"PATH\" an expanded token will be added.\n"
            "\tThe <tag> can also be a GPSd URL, e.g.\n"
            "\t\t\"-K gpsd,lat,lon\" (report lat and lon keys from local gpsd)\n"
            "\t\t\"-K loc=gpsd,lat,lon\" (report lat and lon in loc object)\n"
            "\t\t\"-K gpsd\" (full json TPV report, in default \"gps\" object)\n"
            "\t\t\"-K foo=gpsd://127.0.0.1:2947\" (with key and address)\n"
            "\t\t\"-K bar=gpsd,nmea\" (NMEA default GPGGA report)\n"
            "\t\t\"-K rmc=gpsd,nmea,filter='$GPRMC'\" (NMEA GPRMC report)\n"
            "\tAlso <tag> can be a generic tcp address, e.g.\n"
            "\t\t\"-K foo=tcp:localhost:4000\" (read lines as TCP client)\n"
            "\t\t\"-K bar=tcp://127.0.0.1:3000,init='subscribe tags\\r\\n'\"\n"
            "\t\t\"-K baz=tcp://127.0.0.1:5000,filter='a prefix to match'\"\n");
    exit(0);
}

_Noreturn
static void help_meta(void)
{
    term_help_printf(
            "\t\t= Meta information option =\n"
            "  [-M time[:<options>]|protocol|level|noise[:<secs>]|stats|bits] Add various metadata to every output line.\n"
            "\tUse \"time\" to add current date and time meta data (preset for live inputs).\n"
            "\tUse \"time:rel\" to add sample position meta data (preset for read-file and stdin).\n"
            "\tUse \"time:unix\" to show the seconds since unix epoch as time meta data. This is always UTC.\n"
            "\tUse \"time:iso\" to show the time with ISO-8601 format (YYYY-MM-DD\"T\"hh:mm:ss).\n"
            "\tUse \"time:off\" to remove time meta data.\n"
            "\tUse \"time:usec\" to add microseconds to date time meta data.\n"
            "\tUse \"time:tz\" to output time with timezone offset.\n"
            "\tUse \"time:utc\" to output time in UTC.\n"
            "\t\t(this may also be accomplished by invocation with TZ environment variable set).\n"
            "\t\t\"usec\" and \"utc\" can be combined with other options, eg. \"time:iso:utc\" or \"time:unix:usec\".\n"
            "\tUse \"replay[:N]\" to replay file inputs at (N-times) realtime.\n"
            "\tUse \"protocol\" / \"noprotocol\" to output the decoder protocol number meta data.\n"
            "\tUse \"level\" to add Modulation, Frequency, RSSI, SNR, and Noise meta data.\n"
            "\tUse \"noise[:<secs>]\" to report estimated noise level at intervals (default: 10 seconds).\n"
            "\tUse \"stats[:[<level>][:<interval>]]\" to report statistics (default: 600 seconds).\n"
            "\t  level 0: no report, 1: report successful devices, 2: report active devices, 3: report all\n"
            "\tUse \"bits\" to add bit representation to code outputs (for debug).\n");
    exit(0);
}

_Noreturn
static void help_read(void)
{
    term_help_printf(
            "\t\t= Read file option =\n"
            "  [-r <filename>] Read data from input file instead of a receiver\n"
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
            "\tforced overrides: am:s16:path/filename.ext\n\n"
            "\tReading from pipes also support format options.\n"
            "\tE.g reading complex 32-bit float: CU32:-\n");
    exit(0);
}

_Noreturn
static void help_write(void)
{
    term_help_printf(
            "\t\t= Write file option =\n"
            "  [-w <filename>] Save data stream to output file (a '-' dumps samples to stdout)\n"
            "  [-W <filename>] Save data stream to output file, overwrite existing file\n"
            "\tParameters are detected from the full path, file name, and extension.\n\n"
            "\tFile content and format are detected as parameters, possible options are:\n"
            "\t'cu8', 'cs8', 'cs16', 'cf32' ('IQ' implied),\n"
            "\t'am.s16', 'am.f32', 'fm.s16', 'fm.f32',\n"
            "\t'i.f32', 'q.f32', 'logic.u8', 'ook', and 'vcd'.\n\n"
            "\tParameters must be separated by non-alphanumeric chars and are case-insensitive.\n"
            "\tOverrides can be prefixed, separated by colon (':')\n\n"
            "\tE.g. default detection by extension: path/filename.am.s16\n"
            "\tforced overrides: am:s16:path/filename.ext\n");
    exit(0);
}

static void sdr_callback(unsigned char *iq_buf, uint32_t len, void *ctx)
{
    //fprintf(stderr, "sdr_callback... %u\n", len);
    r_cfg_t *cfg = ctx;
    struct dm_state *demod = cfg->demod;
    char time_str[LOCAL_TIME_BUFLEN];
    unsigned long n_samples;

    // do this here and not in sdr_handler so realtime replay can use rtl_tcp output
    for (void **iter = cfg->raw_handler.elems; iter && *iter; ++iter) {
        raw_output_t *output = *iter;
        raw_output_frame(output, iq_buf, len);
    }

    if ((cfg->bytes_to_read > 0) && (cfg->bytes_to_read <= len)) {
        len = cfg->bytes_to_read;
        cfg->exit_async = 1;
    }

    // save last frame time to see if a new second started
    time_t last_frame_sec = demod->now.tv_sec;
    get_time_now(&demod->now);

    n_samples = len / demod->sample_size;
    if (n_samples * demod->sample_size != len) {
        print_log(LOG_WARNING, __func__, "Sample buffer length not aligned to sample size!");
    }
    if (!n_samples) {
        print_log(LOG_WARNING, __func__, "Sample buffer too short!");
        return; // keep the watchdog timer running
    }

    // age the frame position if there is one
    if (demod->frame_start_ago)
        demod->frame_start_ago += n_samples;
    if (demod->frame_end_ago)
        demod->frame_end_ago += n_samples;

    cfg->watchdog++; // reset the frame acquire watchdog

    if (demod->samp_grab) {
        samp_grab_push(demod->samp_grab, iq_buf, len);
    }

    // AM demodulation
    float avg_db;
    if (demod->sample_size == 2) { // CU8
        if (demod->use_mag_est) {
            //magnitude_true_cu8(iq_buf, demod->buf.temp, n_samples);
            avg_db = magnitude_est_cu8(iq_buf, demod->buf.temp, n_samples);
        }
        else { // amp est
            avg_db = envelope_detect(iq_buf, demod->buf.temp, n_samples);
        }
    } else { // CS16
        //magnitude_true_cs16((int16_t *)iq_buf, demod->buf.temp, n_samples);
        avg_db = magnitude_est_cs16((int16_t *)iq_buf, demod->buf.temp, n_samples);
    }

    //fprintf(stderr, "noise level: %.1f dB current: %.1f dB min level: %.1f dB\n", demod->noise_level, avg_db, demod->min_level_auto);
    if (demod->min_level_auto == 0.0f) {
        demod->min_level_auto = demod->min_level;
    }
    if (demod->noise_level == 0.0f) {
        demod->noise_level = demod->min_level_auto - 3.0f;
    }
    int noise_only = avg_db < demod->noise_level + 3.0f; // or demod->min_level_auto?
    // always process frames if loader, dumper, or analyzers are in use, otherwise skip silent frames
    int process_frame = demod->squelch_offset <= 0 || !noise_only || demod->load_info.format || demod->analyze_pulses || demod->dumper.len || demod->samp_grab;
    if (noise_only) {
        demod->noise_level = (demod->noise_level * 7 + avg_db) / 8; // fast fall over 8 frames
        // If auto_level and noise level well below min_level and significant change in noise level
        if (demod->auto_level > 0 && demod->noise_level < demod->min_level - 3.0f
                && fabsf(demod->min_level_auto - demod->noise_level - 3.0f) > 1.0f) {
            demod->min_level_auto = demod->noise_level + 3.0f;
            print_logf(LOG_WARNING, "Auto Level", "Estimated noise level is %.1f dB, adjusting minimum detection level to %.1f dB",
                    demod->noise_level, demod->min_level_auto);
            pulse_detect_set_levels(demod->pulse_detect, demod->use_mag_est, demod->level_limit, demod->min_level_auto, demod->min_snr, demod->detect_verbosity);
        }
    } else {
        demod->noise_level = (demod->noise_level * 31 + avg_db) / 32; // slow rise over 32 frames
    }
    // Report noise every report_noise seconds, but only for the first frame that second
    if (cfg->report_noise && last_frame_sec != demod->now.tv_sec && demod->now.tv_sec % cfg->report_noise == 0) {
        print_logf(LOG_WARNING, "Auto Level", "Current %s level %.1f dB, estimated noise %.1f dB",
                noise_only ? "noise" : "signal", avg_db, demod->noise_level);
    }

    if (process_frame)
    baseband_low_pass_filter(demod->buf.temp, demod->am_buf, n_samples, &demod->lowpass_filter_state);

    // FM demodulation
    // Select the correct fsk pulse detector
    unsigned fpdm = cfg->fsk_pulse_detect_mode;
    if (cfg->fsk_pulse_detect_mode == FSK_PULSE_DETECT_AUTO) {
        if (cfg->frequency[cfg->frequency_index] > FSK_PULSE_DETECTOR_LIMIT)
            fpdm = FSK_PULSE_DETECT_NEW;
        else
            fpdm = FSK_PULSE_DETECT_OLD;
    }

    if (demod->enable_FM_demod && process_frame) {
        float low_pass = demod->low_pass != 0.0f ? demod->low_pass : fpdm ? 0.2f : 0.1f;
        if (demod->sample_size == 2) { // CU8
            baseband_demod_FM(iq_buf, demod->buf.fm, n_samples, cfg->samp_rate, low_pass, &demod->demod_FM_state);
        } else { // CS16
            baseband_demod_FM_cs16((int16_t *)iq_buf, demod->buf.fm, n_samples, cfg->samp_rate, low_pass, &demod->demod_FM_state);
        }
    }

    // Handle special input formats
    if (demod->load_info.format == S16_AM) { // The IQ buffer is really AM demodulated data
        if (len > sizeof(demod->am_buf))
            FATAL("Buffer too small");
        memcpy(demod->am_buf, iq_buf, len);
    } else if (demod->load_info.format == S16_FM) { // The IQ buffer is really FM demodulated data
        // we would need AM for the envelope too
        if (len > sizeof(demod->buf.fm))
            FATAL("Buffer too small");
        memcpy(demod->buf.fm, iq_buf, len);
    }

    int d_events = 0; // Sensor events successfully detected
    if (demod->r_devs.len || demod->analyze_pulses || demod->dumper.len || demod->samp_grab) {
        // Detect a package and loop through demodulators with pulse data
        int package_type = PULSE_DATA_OOK;  // Just to get us started
        for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
            file_info_t const *dumper = *iter;
            if (dumper->format == U8_LOGIC) {
                memset(demod->u8_buf, 0, n_samples);
                break;
            }
        }
        while (package_type && process_frame) {
            int p_events = 0; // Sensor events successfully detected per package
            package_type = pulse_detect_package(demod->pulse_detect, demod->am_buf, demod->buf.fm, n_samples, cfg->samp_rate, cfg->input_pos, &demod->pulse_data, &demod->fsk_pulse_data, fpdm);
            if (package_type) {
                // new package: set a first frame start if we are not tracking one already
                if (!demod->frame_start_ago)
                    demod->frame_start_ago = demod->pulse_data.start_ago;
                // always update the last frame end
                demod->frame_end_ago = demod->pulse_data.end_ago;
            }
            if (package_type == PULSE_DATA_OOK) {
                calc_rssi_snr(cfg, &demod->pulse_data);
                if (demod->analyze_pulses) fprintf(stderr, "Detected OOK package\t%s\n", time_pos_str(cfg, demod->pulse_data.start_ago, time_str));

                p_events += run_ook_demods(&demod->r_devs, &demod->pulse_data);
                cfg->frames_count++;
                cfg->frames_events += p_events > 0;

                for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
                    file_info_t const *dumper = *iter;
                    if (dumper->format == VCD_LOGIC) pulse_data_print_vcd(dumper->file, &demod->pulse_data, '\'');
                    if (dumper->format == U8_LOGIC) pulse_data_dump_raw(demod->u8_buf, n_samples, cfg->input_pos, &demod->pulse_data, 0x02);
                    if (dumper->format == PULSE_OOK) pulse_data_dump(dumper->file, &demod->pulse_data);
                }

                if (cfg->verbosity >= LOG_TRACE) pulse_data_print(&demod->pulse_data);
                if (cfg->raw_mode == 1 || (cfg->raw_mode == 2 && p_events == 0) || (cfg->raw_mode == 3 && p_events > 0)) {
                    data_t *data = pulse_data_print_data(&demod->pulse_data);
                    event_occurred_handler(cfg, data);
                }
                if (demod->analyze_pulses && (cfg->grab_mode <= 1 || (cfg->grab_mode == 2 && p_events == 0) || (cfg->grab_mode == 3 && p_events > 0)) ) {
                    pulse_analyzer(&demod->pulse_data, package_type);
                }

            } else if (package_type == PULSE_DATA_FSK) {
                calc_rssi_snr(cfg, &demod->fsk_pulse_data);
                if (demod->analyze_pulses) fprintf(stderr, "Detected FSK package\t%s\n", time_pos_str(cfg, demod->fsk_pulse_data.start_ago, time_str));

                p_events += run_fsk_demods(&demod->r_devs, &demod->fsk_pulse_data);
                cfg->frames_fsk++;
                cfg->frames_events += p_events > 0;

                for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
                    file_info_t const *dumper = *iter;
                    if (dumper->format == VCD_LOGIC) pulse_data_print_vcd(dumper->file, &demod->fsk_pulse_data, '"');
                    if (dumper->format == U8_LOGIC) pulse_data_dump_raw(demod->u8_buf, n_samples, cfg->input_pos, &demod->fsk_pulse_data, 0x04);
                    if (dumper->format == PULSE_OOK) pulse_data_dump(dumper->file, &demod->fsk_pulse_data);
                }

                if (cfg->verbosity >= LOG_TRACE) pulse_data_print(&demod->fsk_pulse_data);
                if (cfg->raw_mode == 1 || (cfg->raw_mode == 2 && p_events == 0) || (cfg->raw_mode == 3 && p_events > 0)) {
                    data_t *data = pulse_data_print_data(&demod->fsk_pulse_data);
                    event_occurred_handler(cfg, data);
                }
                if (demod->analyze_pulses && (cfg->grab_mode <= 1 || (cfg->grab_mode == 2 && p_events == 0) || (cfg->grab_mode == 3 && p_events > 0))) {
                    pulse_analyzer(&demod->fsk_pulse_data, package_type);
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
    }

    if (demod->am_analyze) {
        am_analyze(demod->am_analyze, demod->am_buf, n_samples, cfg->verbosity >= LOG_INFO, NULL);
    }

    for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
        file_info_t const *dumper = *iter;
        if (!dumper->file
                || dumper->format == VCD_LOGIC
                || dumper->format == PULSE_OOK)
            continue;
        uint8_t *out_buf = iq_buf;  // Default is to dump IQ samples
        unsigned long out_len = n_samples * demod->sample_size;

        if (dumper->format == CU8_IQ) {
            if (demod->sample_size == 4) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((uint8_t *)demod->buf.temp)[n] = (((int16_t *)iq_buf)[n] / 256) + 128; // scale Q0.15 to Q0.7
                out_buf = (uint8_t *)demod->buf.temp;
                out_len = n_samples * 2 * sizeof(uint8_t);
            }
        }
        else if (dumper->format == CS16_IQ) {
            if (demod->sample_size == 2) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((int16_t *)demod->buf.temp)[n] = (iq_buf[n] * 256) - 32768; // scale Q0.7 to Q0.15
                out_buf = (uint8_t *)demod->buf.temp; // this buffer is too small if out_block_size is large
                out_len = n_samples * 2 * sizeof(int16_t);
            }
        }
        else if (dumper->format == CS8_IQ) {
            if (demod->sample_size == 2) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((int8_t *)demod->buf.temp)[n] = (iq_buf[n] - 128);
            }
            else if (demod->sample_size == 4) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((int8_t *)demod->buf.temp)[n] = ((int16_t *)iq_buf)[n] >> 8;
            }
            out_buf = (uint8_t *)demod->buf.temp;
            out_len = n_samples * 2 * sizeof(int8_t);
        }
        else if (dumper->format == CF32_IQ) {
            if (demod->sample_size == 2) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((float *)demod->buf.temp)[n] = (iq_buf[n] - 128) / 128.0f;
            }
            else if (demod->sample_size == 4) {
                for (unsigned long n = 0; n < n_samples * 2; ++n)
                    ((float *)demod->buf.temp)[n] = ((int16_t *)iq_buf)[n] / 32768.0f;
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
                demod->f32_buf[n] = demod->am_buf[n] * (1.0f / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == F32_FM) {
            for (unsigned long n = 0; n < n_samples; ++n)
                demod->f32_buf[n] = demod->buf.fm[n] * (1.0f / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == F32_I) {
            if (demod->sample_size == 2)
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = (iq_buf[n * 2] - 128) * (1.0f / 0x80); // scale from Q0.7
            else
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = ((int16_t *)iq_buf)[n * 2] * (1.0f / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == F32_Q) {
            if (demod->sample_size == 2)
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = (iq_buf[n * 2 + 1] - 128) * (1.0f / 0x80); // scale from Q0.7
            else
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = ((int16_t *)iq_buf)[n * 2 + 1] * (1.0f / 0x8000); // scale from Q0.15
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == U8_LOGIC) { // state data
            out_buf = demod->u8_buf;
            out_len = n_samples;
        }

        if (fwrite(out_buf, 1, out_len, dumper->file) != out_len) {
            print_log(LOG_ERROR, __func__, "Short write, samples lost, exiting!");
            cfg->exit_async = 1;
        }
    }

    cfg->input_pos += n_samples;
    if (cfg->bytes_to_read > 0)
        cfg->bytes_to_read -= len;

    if (cfg->after_successful_events_flag && (d_events > 0)) {
        if (cfg->after_successful_events_flag == 1) {
            cfg->exit_async = 1;
        }
        else {
            cfg->hop_now = 1;
        }
    }

    time_t rawtime;
    time(&rawtime);
    // choose hop_index as frequency_index, if there are too few hop_times use the last one
    int hop_index = cfg->hop_times > cfg->frequency_index ? cfg->frequency_index : cfg->hop_times - 1;
    if (cfg->hop_times > 0 && cfg->frequencies > 1
            && difftime(rawtime, cfg->hop_start_time) >= cfg->hop_time[hop_index]) {
        cfg->hop_now = 1;
    }
    if (cfg->duration > 0 && rawtime >= cfg->stop_time) {
        cfg->exit_async = 1;
        print_log(LOG_CRITICAL, __func__, "Time expired, exiting!");
    }
    if (cfg->stats_now || (cfg->report_stats && cfg->stats_interval && rawtime >= cfg->stats_time)) {
        event_occurred_handler(cfg, create_report_data(cfg, cfg->stats_now ? 3 : cfg->report_stats));
        flush_report_data(cfg);
        if (rawtime >= cfg->stats_time)
            cfg->stats_time += cfg->stats_interval;
        if (cfg->stats_now)
            cfg->stats_now--;
    }

    if (cfg->hop_now && !cfg->exit_async) {
        cfg->hop_now = 0;
        time(&cfg->hop_start_time);
        cfg->frequency_index = (cfg->frequency_index + 1) % cfg->frequencies;
        sdr_set_center_freq(cfg->dev, cfg->frequency[cfg->frequency_index], 1);
    }
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

#define OPTSTRING "hVvqD:c:x:z:p:a:AI:S:m:M:r:w:W:l:d:t:f:H:g:s:b:n:R:X:F:K:C:T:UGy:E:Y:"

// these should match the short options exactly
static struct conf_keywords const conf_keywords[] = {
        {"help", 'h'},
        {"verbose", 'v'},
        {"version", 'V'},
        {"config_file", 'c'},
        {"report_meta", 'M'},
        {"device", 'd'},
        {"device_mode", 'D'},
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
        {"pulse_detect", 'Y'},
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
            help_device_selection();

        cfg->dev_query = arg;
        break;
    case 'D':
        if (!arg)
            help_device_mode();

        if (strcmp(arg, "quit") == 0) {
            cfg->dev_mode = DEVICE_MODE_RESTART;
        }
        else if (strcmp(arg, "restart") == 0) {
            cfg->dev_mode = DEVICE_MODE_RESTART;
        }
        else if (strcmp(arg, "pause") == 0) {
            cfg->dev_mode = DEVICE_MODE_PAUSE;
        }
        else if (strcmp(arg, "manual") == 0) {
            cfg->dev_mode = DEVICE_MODE_MANUAL;
        }
        else {
            fprintf(stderr, "Invalid input device run mode: %s\n", arg);
            help_device_mode();
        }
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
        if (cfg->frequencies < MAX_FREQS) {
            uint32_t sr = atouint32_metric(arg, "-f: ");
            /* If the frequency is above 800MHz sample at 1MS/s */
            if ((sr > FSK_PULSE_DETECTOR_LIMIT) && (cfg->samp_rate == DEFAULT_SAMPLE_RATE)) {
                cfg->samp_rate = 1000000;
                fprintf(stderr, "\nNew defaults active, use \"-Y classic -s 250k\" if you need the old defaults\n\n");
            }
            cfg->frequency[cfg->frequencies++] = sr;
        } else
            fprintf(stderr, "Max number of frequencies reached %d\n", MAX_FREQS);
        break;
    case 'H':
        if (cfg->hop_times < MAX_FREQS)
            cfg->hop_time[cfg->hop_times++] = atoi_time(arg, "-H: ");
        else
            fprintf(stderr, "Max number of hop times reached %d\n", MAX_FREQS);
        break;
    case 'g':
        if (!arg)
            help_gain();

        free(cfg->gain_str);
        cfg->gain_str = strdup(arg);
        if (!cfg->gain_str)
            FATAL_STRDUP("parse_conf_option()");
        break;
    case 'G':
        fprintf(stderr, "register_all (-G) is deprecated. Use -R or a config file to enable additional protocols.\n");
        exit(1);
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
        n = 1000;
        if (arg && atoi(arg) > 0)
            n = atoi(arg);
        fprintf(stderr, "\n\tLevel limit has changed from \"-l %d\" to \"-Y level=%.1f\" in dB.\n\n", n, AMP_TO_DB(n));
        exit(1);
        break;
    case 'n':
        cfg->bytes_to_read = atouint32_metric(arg, "-n: ") * 2;
        break;
    case 'a':
        if (atobv(arg, 1) == 42 && !cfg->demod->am_analyze) {
            cfg->demod->am_analyze = am_analyze_create();
        }
        else {
            fprintf(stderr, "\n\tUse -a for testing only. Enable if you know how ;)\n\n");
            exit(1);
        }
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
        // TODO: file_info_check_read()
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
        if (!arg)
            usage(1);
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

        if (!strncasecmp(arg, "time", 4)) {
            char *p = arg_param(arg);
            // time  time:1  time:on  time:yes
            // time:0  time:off  time:no
            // time:rel
            // time:unix
            // time:iso
            // time:...:usec  time:...:sec
            // time:...:utc  time:...:local
            cfg->report_time = REPORT_TIME_DATE;
            while (p && *p) {
                if (!strncasecmp(p, "0", 1) || !strncasecmp(p, "no", 2) || !strncasecmp(p, "off", 3))
                    cfg->report_time = REPORT_TIME_OFF;
                else if (!strncasecmp(p, "1", 1) || !strncasecmp(p, "yes", 3) || !strncasecmp(p, "on", 2))
                    cfg->report_time = REPORT_TIME_DATE;
                else if (!strncasecmp(p, "rel", 3))
                    cfg->report_time = REPORT_TIME_SAMPLES;
                else if (!strncasecmp(p, "unix", 4))
                    cfg->report_time = REPORT_TIME_UNIX;
                else if (!strncasecmp(p, "iso", 3))
                    cfg->report_time = REPORT_TIME_ISO;
                else if (!strncasecmp(p, "usec", 4))
                    cfg->report_time_hires = 1;
                else if (!strncasecmp(p, "sec", 3))
                    cfg->report_time_hires = 0;
                else if (!strncasecmp(p, "tz", 2))
                    cfg->report_time_tz = 1;
                else if (!strncasecmp(p, "notz", 4))
                    cfg->report_time_tz = 0;
                else if (!strncasecmp(p, "utc", 3))
                    cfg->report_time_utc = 1;
                else if (!strncasecmp(p, "local", 5))
                    cfg->report_time_utc = 0;
                else {
                    fprintf(stderr, "Unknown time format option: %s\n", p);
                    help_meta();
                }

                p = arg_param(p);
            }
            // fprintf(stderr, "time format: %d, usec:%d utc:%d\n", cfg->report_time, cfg->report_time_hires, cfg->report_time_utc);
        }

        // TODO: old time options, remove someday
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
        else if (!strncasecmp(arg, "noise", 5))
            cfg->report_noise = atoiv(arg_param(arg), 10); // atoi_time_default()
        else if (!strcasecmp(arg, "bits"))
            cfg->verbose_bits = 1;
        else if (!strcasecmp(arg, "description"))
            cfg->report_description = 1;
        else if (!strcasecmp(arg, "newmodel"))
            fprintf(stderr, "newmodel option (-M) is deprecated.\n");
        else if (!strcasecmp(arg, "oldmodel"))
            fprintf(stderr, "oldmodel option (-M) is deprecated.\n");
        else if (!strncasecmp(arg, "stats", 5)) {
            // there also should be options to set whether to flush on report
            char *p = arg_param(arg);
            cfg->report_stats = atoiv(p, 1);
            cfg->stats_interval = atoiv(arg_param(p), 600); // atoi_time_default()
            time(&cfg->stats_time);
            cfg->stats_time += cfg->stats_interval;
        }
        else if (!strncasecmp(arg, "replay", 6))
            cfg->in_replay = atobv(arg_param(arg), 1);
        else
            cfg->report_meta = atobv(arg, 1);
        break;
    case 'z':
        fprintf(stderr, "override_short (-z) is deprecated.\n");
        break;
    case 'x':
        fprintf(stderr, "override_long (-x) is deprecated.\n");
        break;
    case 'R':
        if (!arg)
            help_protocols(cfg->devices, cfg->num_r_devices, 0);

        // use arg of 'v', 'vv', 'vvv' as global device verbosity
        if (*arg == 'v') {
            int decoder_verbosity = 0;
            for (int i = 0; arg[i] == 'v'; ++i) {
                decoder_verbosity += 1;
            }
            (void)decoder_verbosity; // FIXME: use this
            break;
        }

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
        else if (strncmp(arg, "log", 3) == 0) {
            add_log_output(cfg, arg_param(arg));
            cfg->has_logout = 1;
        }
        else if (strncmp(arg, "kv", 2) == 0) {
            add_kv_output(cfg, arg_param(arg));
            cfg->has_logout = 1;
        }
        else if (strncmp(arg, "mqtt", 4) == 0) {
            add_mqtt_output(cfg, arg);
        }
        else if (strncmp(arg, "influx", 6) == 0) {
            add_influx_output(cfg, arg);
        }
        else if (strncmp(arg, "syslog", 6) == 0) {
            add_syslog_output(cfg, arg_param(arg));
        }
        else if (strncmp(arg, "http", 4) == 0) {
            add_http_output(cfg, arg_param(arg));
        }
        else if (strncmp(arg, "trigger", 7) == 0) {
            add_trigger_output(cfg, arg_param(arg));
        }
        else if (strncmp(arg, "null", 4) == 0) {
            add_null_output(cfg, arg_param(arg));
        }
        else if (strncmp(arg, "rtl_tcp", 7) == 0) {
            add_rtltcp_output(cfg, arg_param(arg));
        }
        else {
            fprintf(stderr, "Invalid output format: %s\n", arg);
            usage(1);
        }
        break;
    case 'K':
        if (!arg)
            help_tags();
        add_data_tag(cfg, arg);
        break;
    case 'C':
        if (!arg)
            usage(1);
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
            fprintf(stderr, "Invalid conversion mode: %s\n", arg);
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
    case 'Y':
        if (!arg)
            usage(1);
        char const *p = arg;
        while (p && *p) {
            char const *val = NULL;
            if (kwargs_match(p, "autolevel", &val))
                cfg->demod->auto_level = atoiv(val, 1); // arg_float_default(p + 9, "-Y autolevel: ");
            else if (kwargs_match(p, "squelch", &val))
                cfg->demod->squelch_offset = atoiv(val, 1); // arg_float_default(p + 7, "-Y squelch: ");
            else if (kwargs_match(p, "auto", &val))
                cfg->fsk_pulse_detect_mode = FSK_PULSE_DETECT_AUTO;
            else if (kwargs_match(p, "classic", &val))
                cfg->fsk_pulse_detect_mode = FSK_PULSE_DETECT_OLD;
            else if (kwargs_match(p, "minmax", &val))
                cfg->fsk_pulse_detect_mode = FSK_PULSE_DETECT_NEW;
            else if (kwargs_match(p, "ampest", &val))
                cfg->demod->use_mag_est = 0;
            else if (kwargs_match(p, "verbose", &val))
                cfg->demod->detect_verbosity++;
            else if (kwargs_match(p, "magest", &val))
                cfg->demod->use_mag_est = 1;
            else if (kwargs_match(p, "level", &val))
                cfg->demod->level_limit = arg_float(val, "-Y level: ");
            else if (kwargs_match(p, "minlevel", &val))
                cfg->demod->min_level = arg_float(val, "-Y minlevel: ");
            else if (kwargs_match(p, "minsnr", &val))
                cfg->demod->min_snr = arg_float(val, "-Y minsnr: ");
            else if (kwargs_match(p, "filter", &val))
                cfg->demod->low_pass = arg_float(val, "-Y filter: ");
            else {
                fprintf(stderr, "Unknown pulse detector setting: %s\n", p);
                usage(1);
            }
            p = kwargs_skip(p);
        }
        break;
    case 'E':
        if (arg && !strcmp(arg, "hop")) {
            cfg->after_successful_events_flag = 2;
        }
        else if (arg && !strcmp(arg, "quit")) {
            cfg->after_successful_events_flag = 1;
        }
        else {
            cfg->after_successful_events_flag = atobv(arg, 1);
        }
        break;
    default:
        usage(1);
        break;
    }
}

static r_cfg_t g_cfg;

// TODO: SIGINFO is not in POSIX...
#ifndef SIGINFO
#define SIGINFO 29
#endif

// NOTE: printf is not async safe per signal-safety(7)
// writes a static string, without the terminating zero, to stderr, ignores return value
#define write_err(s) (void)!write(STDERR_FILENO, (s), sizeof(s) - 1)

#ifdef _WIN32
BOOL WINAPI
console_handler(int signum)
{
    if (CTRL_C_EVENT == signum) {
        write_err("Signal caught, exiting!\n");
        g_cfg.exit_async = 1;
        // Uninstall handler, next Ctrl-C is a hard abort
        SetConsoleCtrlHandler((PHANDLER_ROUTINE)console_handler, FALSE);
        return TRUE;
    }
    else if (CTRL_BREAK_EVENT == signum) {
        write_err("CTRL-BREAK detected, hopping to next frequency (-f). Use CTRL-C to quit.\n");
        g_cfg.hop_now = 1;
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
    else if (signum == SIGINFO/* TODO: maybe SIGUSR1 */) {
        g_cfg.stats_now++;
        return;
    }
    else if (signum == SIGUSR1) {
        g_cfg.hop_now = 1;
        return;
    }
    else {
        write_err("Signal caught, exiting!\n");
    }
    g_cfg.exit_async = 1;

    // Uninstall handler, next Ctrl-C is a hard abort
    struct sigaction sigact;
    sigact.sa_handler = NULL;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGQUIT, &sigact, NULL);
    sigaction(SIGPIPE, &sigact, NULL);
}
#endif

static void sdr_handler(struct mg_connection *nc, int ev_type, void *ev_data)
{
    //fprintf(stderr, "%s: %d, %d, %p, %p\n", __func__, nc->sock, ev_type, nc->user_data, ev_data);
    // only process for the dummy nc
    if (nc->sock != INVALID_SOCKET || ev_type != MG_EV_POLL)
        return;
    r_cfg_t *cfg     = nc->user_data;
    sdr_event_t *ev = ev_data;
    //fprintf(stderr, "sdr_handler...\n");

    data_t *data = NULL;
    if (ev->ev & SDR_EV_RATE) {
        // cfg->samp_rate = ev->sample_rate;
        data = data_append(data,
                "sample_rate", "", DATA_INT, ev->sample_rate,
                NULL);
    }
    if (ev->ev & SDR_EV_CORR) {
        // cfg->ppm_error = ev->freq_correction;
        data = data_append(data,
                "freq_correction", "", DATA_INT, ev->freq_correction,
                NULL);
    }
    if (ev->ev & SDR_EV_FREQ) {
        // cfg->center_frequency = ev->center_frequency;
        data = data_append(data,
                "center_frequency", "", DATA_INT, ev->center_frequency,
                "frequencies", "", DATA_COND, cfg->frequencies > 1, DATA_ARRAY, data_array(cfg->frequencies, DATA_INT, cfg->frequency),
                "hop_times", "", DATA_COND, cfg->frequencies > 1, DATA_ARRAY, data_array(cfg->hop_times, DATA_INT, cfg->hop_time),
                NULL);
    }
    if (ev->ev & SDR_EV_GAIN) {
        data = data_append(data,
                "gain", "", DATA_STRING, ev->gain_str,
                NULL);
    }
    if (data) {
        event_occurred_handler(cfg, data);
    }

    if (ev->ev == SDR_EV_DATA) {
        cfg->samp_rate        = ev->sample_rate;
        cfg->center_frequency = ev->center_frequency;
        sdr_callback((unsigned char *)ev->buf, ev->len, cfg);
    }

    if (cfg->exit_async) {
        if (cfg->verbosity >= 2)
            print_log(LOG_INFO, "Input", "sdr_handler exit");
        sdr_stop(cfg->dev);
        cfg->exit_async++;
    }
}

// note that this function is called in a different thread
static void acquire_callback(sdr_event_t *ev, void *ctx)
{
    //struct timeval now;
    //get_time_now(&now);
    //fprintf(stderr, "%ld.%06ld acquire_callback...\n", (long)now.tv_sec, (long)now.tv_usec);

    struct mg_mgr *mgr = ctx;

    // TODO: We should run the demod here to unblock the event loop

    // thread-safe dispatch, ev_data is the iq buffer pointer and length
    //fprintf(stderr, "acquire_callback bc send...\n");
    mg_broadcast(mgr, sdr_handler, (void *)ev, sizeof(*ev));
    //fprintf(stderr, "acquire_callback bc done...\n");
}

static int start_sdr(r_cfg_t *cfg)
{
    int r;
    r = sdr_open(&cfg->dev, cfg->dev_query, cfg->verbosity);
    if (r < 0) {
        return -1; // exit(2);
    }
    cfg->dev_info = sdr_get_dev_info(cfg->dev);
    cfg->demod->sample_size = sdr_get_sample_size(cfg->dev);
    // cfg->demod->sample_signed = sdr_get_sample_signed(cfg->dev);

    /* Set the sample rate */
    r = sdr_set_sample_rate(cfg->dev, cfg->samp_rate, 1); // always verbose

    if (cfg->verbosity || cfg->demod->level_limit < 0.0)
        print_logf(LOG_NOTICE, "Input", "Bit detection level set to %.1f%s.", cfg->demod->level_limit, (cfg->demod->level_limit < 0.0 ? "" : " (Auto)"));

    r = sdr_apply_settings(cfg->dev, cfg->settings_str, 1); // always verbose for soapy

    /* Enable automatic gain if gain_str empty (or 0 for RTL-SDR), set manual gain otherwise */
    r = sdr_set_tuner_gain(cfg->dev, cfg->gain_str, 1); // always verbose

    if (cfg->ppm_error) {
        r = sdr_set_freq_correction(cfg->dev, cfg->ppm_error, 1); // always verbose
    }

    /* Reset endpoint before we start reading from it (mandatory) */
    r = sdr_reset(cfg->dev, cfg->verbosity);
    if (r < 0) {
        print_log(LOG_ERROR, "Input", "Failed to reset buffers.");
    }
    r = sdr_activate(cfg->dev);

    if (cfg->verbosity) {
        print_log(LOG_NOTICE, "Input", "Reading samples in async mode...");
    }

    r = sdr_set_center_freq(cfg->dev, cfg->center_frequency, 1); // always verbose

    r = sdr_start(cfg->dev, acquire_callback, (void *)get_mgr(cfg),
            DEFAULT_ASYNC_BUF_NUMBER, cfg->out_block_size);
    if (r < 0) {
        print_logf(LOG_ERROR, "Input", "async start failed (%i).", r);
    }

    cfg->dev_state = DEVICE_STATE_STARTING;
    return r;
}

static void timer_handler(struct mg_connection *nc, int ev, void *ev_data)
{
    //fprintf(stderr, "%s: %d, %d, %p, %p\n", __func__, nc->sock, ev, nc->user_data, ev_data);
    r_cfg_t *cfg = (r_cfg_t *)nc->user_data;
    switch (ev) {
    case MG_EV_TIMER: {
        double now  = *(double *)ev_data;
        (void) now; // unused
        double next = mg_time() + 1.5;
        //fprintf(stderr, "timer event, current time: %.2lf, next timer: %.2lf\n", now, next);
        mg_set_timer(nc, next); // Send us timer event again after 1.5 seconds

        // Did we acquire data frames in the last interval?
        if (cfg->watchdog != 0) {
            if (cfg->dev_state == DEVICE_STATE_STARTING
                    || cfg->dev_state == DEVICE_STATE_GRACE) {
                cfg->dev_state = DEVICE_STATE_STARTED;
            }
            cfg->watchdog = 0;
            break;
        }

        // Upon starting allow more time until the first frame
        if (cfg->dev_state == DEVICE_STATE_STARTING) {
            cfg->dev_state = DEVICE_STATE_GRACE;
            break;
        }
        // We expect a frame at least every 250 ms but didn't get one
        if (cfg->dev_state == DEVICE_STATE_GRACE) {
            if (cfg->dev_mode == DEVICE_MODE_QUIT) {
                print_log(LOG_ERROR, "Input", "Input device start failed, exiting!");
            }
            else if (cfg->dev_mode == DEVICE_MODE_RESTART) {
                print_log(LOG_WARNING, "Input", "Input device start failed, restarting!");
            }
            else { // DEVICE_MODE_PAUSE or DEVICE_MODE_MANUAL
                print_log(LOG_WARNING, "Input", "Input device start failed, pausing!");
            }
        }
        else if (cfg->dev_state == DEVICE_STATE_STARTED) {
            if (cfg->dev_mode == DEVICE_MODE_QUIT) {
                print_log(LOG_ERROR, "Input", "Async read stalled, exiting!");
            }
            else if (cfg->dev_mode == DEVICE_MODE_RESTART) {
                print_log(LOG_WARNING, "Input", "Async read stalled, restarting!");
            }
            else { // DEVICE_MODE_PAUSE or DEVICE_MODE_MANUAL
                print_log(LOG_WARNING, "Input", "Async read stalled, pausing!");
            }
        }
        if (cfg->dev_state != DEVICE_STATE_STOPPED) {
            cfg->exit_async = 1;
            cfg->exit_code = 3;
            sdr_stop(cfg->dev);
            cfg->dev_state = DEVICE_STATE_STOPPED;
        }
        if (cfg->dev_mode == DEVICE_MODE_QUIT) {
            cfg->exit_async = 1;
        }
        if (cfg->dev_mode == DEVICE_MODE_RESTART) {
            start_sdr(cfg);
        }
        // do nothing for DEVICE_MODE_PAUSE or DEVICE_MODE_MANUAL

        break;
    }
    }
}

int main(int argc, char **argv) {
    int r = 0;
    struct dm_state *demod;
    r_cfg_t *cfg = &g_cfg;

    print_version(); // always print the version info
    sdr_redirect_logging();

    r_init_cfg(cfg);

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    demod = cfg->demod;

    // if there is no explicit conf file option look for default conf files
    if (!hasopt('c', argc, argv, OPTSTRING)) {
        parse_conf_try_default_files(cfg);
    }

    parse_conf_args(cfg, argc, argv);
    // apply hop defaults and set first frequency
    if (cfg->frequencies == 0) {
        cfg->frequency[0] = DEFAULT_FREQUENCY;
        cfg->frequencies  = 1;
    }
    cfg->center_frequency = cfg->frequency[cfg->frequency_index];
    if (cfg->frequencies > 1 && cfg->hop_times == 0) {
        cfg->hop_time[cfg->hop_times++] = DEFAULT_HOP_TIME;
    }
    // save sample rate, this should be a hop config too
    uint32_t sample_rate_0 = cfg->samp_rate;

    // add all remaining positional arguments as input files
    while (argc > optind) {
        add_infile(cfg, argv[optind++]);
    }

    pulse_detect_set_levels(demod->pulse_detect, demod->use_mag_est, demod->level_limit, demod->min_level, demod->min_snr, demod->detect_verbosity);

    if (demod->am_analyze) {
        demod->am_analyze->level_limit = DB_TO_AMP(demod->level_limit);
        demod->am_analyze->frequency   = &cfg->center_frequency;
        demod->am_analyze->samp_rate   = &cfg->samp_rate;
        demod->am_analyze->sample_size = &demod->sample_size;
    }

    if (demod->samp_grab) {
        demod->samp_grab->frequency   = &cfg->center_frequency;
        demod->samp_grab->samp_rate   = &cfg->samp_rate;
        demod->samp_grab->sample_size = &demod->sample_size;
    }

    if (cfg->report_time == REPORT_TIME_DEFAULT) {
        if (cfg->in_files.len)
            cfg->report_time = REPORT_TIME_SAMPLES;
        else
            cfg->report_time = REPORT_TIME_DATE;
    }
    if (cfg->report_time_utc) {
#ifdef _WIN32
        putenv("TZ=UTC+0");
        _tzset();
#else
        r = setenv("TZ", "UTC", 1);
        if (r != 0)
            fprintf(stderr, "Unable to set TZ to UTC; error code: %d\n", r);
#endif
    }

    if (!cfg->output_handler.len) {
        add_kv_output(cfg, NULL);
    }
    else if (!cfg->has_logout) {
        // Warn if no log outputs are enabled
        fprintf(stderr, "Use \"-F log\" if you want any messages, warnings, and errors in the console.\n");
    }
    // Change log handler after outputs are set up
    r_redirect_logging(cfg);

    // register default decoders if nothing is configured
    if (!cfg->no_default_devices) {
        register_all_protocols(cfg, 0); // register all defaults
    }

    // check if we need FM demod
    for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        if (r_dev->modulation >= FSK_DEMOD_MIN_VAL) {
          demod->enable_FM_demod = 1;
          break;
        }
    }

    {
        char decoders_str[1024];
        decoders_str[0] = '\0';
        if (cfg->verbosity <= LOG_NOTICE) {
            abuf_t p = {0};
            abuf_init(&p, decoders_str, sizeof(decoders_str));
            // print registered decoder ranges
            abuf_printf(&p, " [");
            for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
                r_device *r_dev = *iter;
                unsigned num = r_dev->protocol_num;
                if (num == 0)
                    continue;
                while (iter[1]
                        && r_dev->protocol_num + 1 == ((r_device *)iter[1])->protocol_num)
                    r_dev = *++iter;
                if (num == r_dev->protocol_num)
                    abuf_printf(&p, " %u", num);
                else
                    abuf_printf(&p, " %u-%u", num, r_dev->protocol_num);
            }
            abuf_printf(&p, " ]");
        }
        print_logf(LOG_CRITICAL, "Protocols", "Registered %zu out of %u device decoding protocols%s",
                demod->r_devs.len, cfg->num_r_devices, decoders_str);
    }

    char const **well_known = well_known_output_fields(cfg);
    start_outputs(cfg, well_known);
    free((void *)well_known);

    if (cfg->out_block_size < MINIMAL_BUF_LENGTH ||
            cfg->out_block_size > MAXIMAL_BUF_LENGTH) {
        print_logf(LOG_ERROR, "Block Size",
                "Output block size wrong value, falling back to default (%d)", DEFAULT_BUF_LENGTH);
        print_logf(LOG_ERROR, "Block Size",
                "Minimal length: %d", MINIMAL_BUF_LENGTH);
        print_logf(LOG_ERROR, "Block Size",
                "Maximal length: %d", MAXIMAL_BUF_LENGTH);
        cfg->out_block_size = DEFAULT_BUF_LENGTH;
    }

    // Special case for streaming test data
    if (cfg->test_data && (!strcasecmp(cfg->test_data, "-") || *cfg->test_data == '@')) {
        FILE *fp;
        char line[INPUT_LINE_MAX];

        if (*cfg->test_data == '@') {
            print_logf(LOG_CRITICAL, "Input", "Reading test data from \"%s\"", &cfg->test_data[1]);
            fp = fopen(&cfg->test_data[1], "r");
        } else {
            print_log(LOG_CRITICAL, "Input", "Reading test data from stdin");
            fp = stdin;
        }
        if (!fp) {
            print_logf(LOG_ERROR, "Input", "Failed to open %s", cfg->test_data);
            exit(1);
        }

        while (fgets(line, INPUT_LINE_MAX, fp)) {
            if (cfg->verbosity >= LOG_NOTICE)
                print_logf(LOG_NOTICE, "Input", "Processing test data \"%s\"...", line);
            r = 0;
            // test a single decoder?
            if (*line == '[') {
                char *e = NULL;
                unsigned d = (unsigned)strtol(&line[1], &e, 10);
                if (!e || *e != ']') {
                    print_logf(LOG_ERROR, "Protocol", "Bad protocol number %.5s.", line);
                    exit(1);
                }
                e++;
                r_device *r_dev = NULL;
                for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
                    r_device *r_dev_i = *iter;
                    if (r_dev_i->protocol_num == d) {
                        r_dev = r_dev_i;
                        break;
                    }
                }
                if (!r_dev) {
                    print_logf(LOG_ERROR, "Protocol", "Unknown protocol number %u.", d);
                    exit(1);
                }
                if (cfg->verbosity >= LOG_NOTICE)
                    print_logf(LOG_NOTICE, "Input", "Verifying test data with device %s.", r_dev->name);
                if (rfraw_check(e)) {
                    pulse_data_t pulse_data = {0};
                    rfraw_parse(&pulse_data, e);
                    list_t single_dev = {0};
                    list_push(&single_dev, r_dev);
                    if (!pulse_data.fsk_f2_est)
                        r += run_ook_demods(&single_dev, &pulse_data);
                    else
                        r += run_fsk_demods(&single_dev, &pulse_data);
                    list_free_elems(&single_dev, NULL);
                } else
                r += pulse_slicer_string(e, r_dev);
                continue;
            }
            // otherwise test all decoders
            if (rfraw_check(line)) {
                pulse_data_t pulse_data = {0};
                rfraw_parse(&pulse_data, line);
                if (!pulse_data.fsk_f2_est)
                    r += run_ook_demods(&demod->r_devs, &pulse_data);
                else
                    r += run_fsk_demods(&demod->r_devs, &pulse_data);
            } else
            for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
                r_device *r_dev = *iter;
                if (cfg->verbosity >= LOG_NOTICE)
                    print_logf(LOG_NOTICE, "Input", "Verifying test data with device %s.", r_dev->name);
                r += pulse_slicer_string(line, r_dev);
            }
        }

        if (*cfg->test_data == '@') {
            fclose(fp);
        }

        r_free_cfg(cfg);
        exit(!r);
    }
    // Special case for string test data
    if (cfg->test_data) {
        r = 0;
        if (rfraw_check(cfg->test_data)) {
            pulse_data_t pulse_data = {0};
            rfraw_parse(&pulse_data, cfg->test_data);
            if (!pulse_data.fsk_f2_est)
                r += run_ook_demods(&demod->r_devs, &pulse_data);
            else
                r += run_fsk_demods(&demod->r_devs, &pulse_data);
        } else
        for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
            r_device *r_dev = *iter;
            if (cfg->verbosity >= LOG_NOTICE)
                print_logf(LOG_NOTICE, "Input", "Verifying test data with device %s.", r_dev->name);
            r += pulse_slicer_string(cfg->test_data, r_dev);
        }
        r_free_cfg(cfg);
        exit(!r);
    }

    // Special case for in files
    if (cfg->in_files.len) {
        unsigned char *test_mode_buf = malloc(DEFAULT_BUF_LENGTH * sizeof(unsigned char));
        if (!test_mode_buf)
            FATAL_MALLOC("test_mode_buf");
        float *test_mode_float_buf = malloc(DEFAULT_BUF_LENGTH / sizeof(int16_t) * sizeof(float));
        if (!test_mode_float_buf)
            FATAL_MALLOC("test_mode_float_buf");

        if (cfg->duration > 0) {
            time(&cfg->stop_time);
            cfg->stop_time += cfg->duration;
        }

        for (void **iter = cfg->in_files.elems; iter && *iter; ++iter) {
            cfg->in_filename = *iter;

            file_info_clear(&demod->load_info); // reset all info
            file_info_parse_filename(&demod->load_info, cfg->in_filename);
            // apply file info or default
            cfg->samp_rate        = demod->load_info.sample_rate ? demod->load_info.sample_rate : sample_rate_0;
            cfg->center_frequency = demod->load_info.center_frequency ? demod->load_info.center_frequency : cfg->frequency[0];

            FILE *in_file;
            if (strcmp(demod->load_info.path, "-") == 0) { // read samples from stdin
                in_file = stdin;
                cfg->in_filename = "<stdin>";
            } else {
                in_file = fopen(demod->load_info.path, "rb");
                if (!in_file) {
                    print_logf(LOG_ERROR, "Input", "Opening file \"%s\" failed!", cfg->in_filename);
                    break;
                }
            }
            print_logf(LOG_CRITICAL, "Input", "Test mode active. Reading samples from file: %s", cfg->in_filename); // Essential information (not quiet)
            if (demod->load_info.format == CU8_IQ
                    || demod->load_info.format == CS8_IQ
                    || demod->load_info.format == S16_AM
                    || demod->load_info.format == S16_FM) {
                demod->sample_size = sizeof(uint8_t) * 2; // CU8, AM, FM
            } else if (demod->load_info.format == CS16_IQ
                    || demod->load_info.format == CF32_IQ) {
                demod->sample_size = sizeof(int16_t) * 2; // CS16, CF32 (after conversion)
            } else if (demod->load_info.format == PULSE_OOK) {
                // ignore
            } else {
                print_logf(LOG_ERROR, "Input", "Input format invalid \"%s\"", file_info_string(&demod->load_info));
                break;
            }
            if (cfg->verbosity >= LOG_NOTICE) {
                print_logf(LOG_NOTICE, "Input", "Input format \"%s\"", file_info_string(&demod->load_info));
            }
            demod->sample_file_pos = 0.0;

            // special case for pulse data file-inputs
            if (demod->load_info.format == PULSE_OOK) {
                while (!cfg->exit_async) {
                    pulse_data_load(in_file, &demod->pulse_data, cfg->samp_rate);
                    if (!demod->pulse_data.num_pulses)
                        break;

                    for (void **iter2 = demod->dumper.elems; iter2 && *iter2; ++iter2) {
                        file_info_t const *dumper = *iter2;
                        if (dumper->format == VCD_LOGIC) {
                            pulse_data_print_vcd(dumper->file, &demod->pulse_data, '\'');
                        } else if (dumper->format == PULSE_OOK) {
                            pulse_data_dump(dumper->file, &demod->pulse_data);
                        } else {
                            print_logf(LOG_ERROR, "Input", "Dumper (%s) not supported on OOK input", dumper->spec);
                            exit(1);
                        }
                    }

                    if (demod->pulse_data.fsk_f2_est) {
                        run_fsk_demods(&demod->r_devs, &demod->pulse_data);
                    }
                    else {
                        int p_events = run_ook_demods(&demod->r_devs, &demod->pulse_data);
                        if (cfg->verbosity >= LOG_DEBUG)
                            pulse_data_print(&demod->pulse_data);
                        if (demod->analyze_pulses && (cfg->grab_mode <= 1 || (cfg->grab_mode == 2 && p_events == 0) || (cfg->grab_mode == 3 && p_events > 0))) {
                            pulse_analyzer(&demod->pulse_data, PULSE_DATA_OOK);
                        }
                    }
                }

                if (in_file != stdin)
                    fclose(in_file = stdin);

                continue;
            }

            // default case for file-inputs
            int n_blocks = 0;
            unsigned long n_read;
            delay_timer_t delay_timer;
            delay_timer_init(&delay_timer);
            do {
                // Replay in realtime if requested
                if (cfg->in_replay) {
                    // per block delay
                    unsigned delay_us = (unsigned)(1000000llu * DEFAULT_BUF_LENGTH / cfg->samp_rate / demod->sample_size / cfg->in_replay);
                    if (demod->load_info.format == CF32_IQ)
                        delay_us /= 2; // adjust for float only reading half as many samples
                    delay_timer_wait(&delay_timer, delay_us);
                }
                // Convert CF32 file to CS16 buffer
                if (demod->load_info.format == CF32_IQ) {
                    n_read = fread(test_mode_float_buf, sizeof(float), DEFAULT_BUF_LENGTH / 2, in_file);
                    // clamp float to [-1,1] and scale to Q0.15
                    for (unsigned long n = 0; n < n_read; n++) {
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

                    // Convert CS8 file to CU8 buffer
                    if (demod->load_info.format == CS8_IQ) {
                        for (unsigned long n = 0; n < n_read; n++) {
                            test_mode_buf[n] = ((int8_t)test_mode_buf[n]) + 128;
                        }
                    }
                }
                if (n_read == 0) break;  // sdr_callback() will Segmentation Fault with len=0
                demod->sample_file_pos = ((float)n_blocks * DEFAULT_BUF_LENGTH + n_read) / cfg->samp_rate / demod->sample_size;
                n_blocks++; // this assumes n_read == DEFAULT_BUF_LENGTH
                sdr_callback(test_mode_buf, n_read, cfg);
            } while (n_read != 0 && !cfg->exit_async);

            // Call a last time with cleared samples to ensure EOP detection
            if (demod->sample_size == 2) { // CU8
                memset(test_mode_buf, 128, DEFAULT_BUF_LENGTH); // 128 is 0 in unsigned data
                // or is 127.5 a better 0 in cu8 data?
                //for (unsigned long n = 0; n < DEFAULT_BUF_LENGTH/2; n++)
                //    ((uint16_t *)test_mode_buf)[n] = 0x807f;
            }
            else { // CF32, CS16
                    memset(test_mode_buf, 0, DEFAULT_BUF_LENGTH);
            }
            demod->sample_file_pos = ((float)n_blocks + 1) * DEFAULT_BUF_LENGTH / cfg->samp_rate / demod->sample_size;
            sdr_callback(test_mode_buf, DEFAULT_BUF_LENGTH, cfg);

            //Always classify a signal at the end of the file
            if (demod->am_analyze)
                am_analyze_classify(demod->am_analyze);
            if (cfg->verbosity >= LOG_NOTICE) {
                print_logf(LOG_NOTICE, "Input", "Test mode file issued %d packets", n_blocks);
            }

            if (in_file != stdin)
                fclose(in_file = stdin);
        }

        close_dumpers(cfg);
        free(test_mode_buf);
        free(test_mode_float_buf);
        r_free_cfg(cfg);
        exit(0);
    }

    // Normal case, no test data, no in files
    if (cfg->sr_filename) {
        print_logf(LOG_ERROR, "Input", "SR writing not recommended for live input");
        exit(1);
    }

#ifndef _WIN32
    struct sigaction sigact;
    sigact.sa_handler = sighandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGQUIT, &sigact, NULL);
    sigaction(SIGPIPE, &sigact, NULL);
    sigaction(SIGUSR1, &sigact, NULL);
    sigaction(SIGINFO, &sigact, NULL);
#else
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)console_handler, TRUE);
#endif

    // TODO: remove this before next release
    print_log(LOG_NOTICE, "Input", "The internals of input handling changed, read about and report problems on PR #1978");

    if (cfg->dev_mode != DEVICE_MODE_MANUAL) {
        r = start_sdr(cfg);
        if (r < 0) {
            exit(2);
        }
    }

    if (cfg->duration > 0) {
        time(&cfg->stop_time);
        cfg->stop_time += cfg->duration;
    }

    time(&cfg->hop_start_time);

    // add dummy socket to receive broadcasts
    struct mg_add_sock_opts opts = {.user_data = cfg};
    struct mg_connection *nc = mg_add_sock_opt(get_mgr(cfg), INVALID_SOCKET, timer_handler, opts);
    // Send us MG_EV_TIMER event after 2.5 seconds
    mg_set_timer(nc, mg_time() + 2.5);

    while (!cfg->exit_async) {
        mg_mgr_poll(cfg->mgr, 500);
    }
    if (cfg->verbosity >= LOG_INFO)
        print_log(LOG_INFO, "rtl_433", "stopping...");
    // final polls to drain the broadcast
    //while (cfg->exit_async < 2) {
    //    mg_mgr_poll(cfg->mgr, 100);
    //}
    sdr_stop(cfg->dev);
    //print_log(LOG_INFO, "rtl_433", "stopped.");

    if (cfg->report_stats > 0) {
        event_occurred_handler(cfg, create_report_data(cfg, cfg->report_stats));
        flush_report_data(cfg);
    }

    if (!cfg->exit_async) {
        print_logf(LOG_ERROR, "rtl_433", "Library error %d, exiting...", r);
        cfg->exit_code = r;
    }

    if (cfg->exit_code >= 0)
        r = cfg->exit_code;
    r_free_cfg(cfg);

    return r >= 0 ? r : -r;
}
