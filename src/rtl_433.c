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
#include <errno.h>
#include <signal.h>

#include "rtl_433.h"
#include "r_private.h"
#include "r_device.h"
#include "rtl_433_devices.h"
#include "r_api.h"
#include "sdr.h"
#include "baseband.h"
#include "pulse_detect.h"
#include "pulse_detect_fsk.h"
#include "pulse_demod.h"
#include "data.h"
#include "r_util.h"
#include "optparse.h"
#include "fileformat.h"
#include "samp_grab.h"
#include "am_analyze.h"
#include "confparse.h"
#include "term_ctl.h"
#include "compat_paths.h"
#include "fatal.h"

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

r_device *flex_create_device(char *spec); // maybe put this in some header file?

static void print_version(void)
{
    fprintf(stderr, "%s\n", version_string());
    fprintf(stderr, "Use -h for usage help and see https://triq.org/ for documentation.\n");
}

static void usage(int exit_code)
{
    term_help_printf(
            "Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.\n"
            "\nUsage:\n"
            "\t\t= General options =\n"
            "  [-V] Output the version string and exit\n"
            "  [-v] Increase verbosity (can be used multiple times).\n"
            "       -v : verbose, -vv : verbose decoders, -vvv : debug decoders, -vvvv : trace decoding).\n"
            "  [-c <path>] Read config options from a file\n"
            "\t\t= Tuner options =\n"
            "  [-d <RTL-SDR USB device index> | :<RTL-SDR USB device serial> | <SoapySDR device query> | rtl_tcp | help]\n"
            "  [-g <gain> | help] (default: auto)\n"
            "  [-t <settings>] apply a list of keyword=value settings for SoapySDR devices\n"
            "       e.g. -t \"antenna=A,bandwidth=4.5M,rfnotch_ctrl=false\"\n"
            "  [-f <frequency>] Receive frequency(s) (default: %i Hz)\n"
            "  [-H <seconds>] Hop interval for polling of multiple frequencies (default: %i seconds)\n"
            "  [-p <ppm_error] Correct rtl-sdr tuner frequency offset error (default: 0)\n"
            "  [-s <sample rate>] Set sample rate (default: %i Hz)\n"
            "\t\t= Demodulator options =\n"
            "  [-R <device> | help] Enable only the specified device decoding protocol (can be used multiple times)\n"
            "       Specify a negative number to disable a device decoding protocol (can be used multiple times)\n"
            "  [-G] Enable blacklisted device decoding protocols, for testing only.\n"
            "  [-X <spec> | help] Add a general purpose decoder (prepend -R 0 to disable all decoders)\n"
            "  [-l <level>] Change detection level used to determine pulses (0-16384) (0=auto) (default: %i)\n"
            "  [-z <value>] Override short value in data decoder\n"
            "  [-x <value>] Override long value in data decoder\n"
            "  [-n <value>] Specify number of samples to take (each sample is 2 bytes: 1 each of I & Q)\n"
            "  [-Y auto | classic | minmax] FSK pulse detector mode.\n"
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
            "  [-F kv | json | csv | mqtt | influx | syslog | null | help] Produce decoded output in given format.\n"
            "       Append output to file with :<filename> (e.g. -F csv:log.csv), defaults to stdout.\n"
            "       Specify host/port for syslog with e.g. -F syslog:127.0.0.1:1514\n"
            "  [-M time[:<options>] | protocol | level | stats | bits | help] Add various meta data to each output.\n"
            "  [-K FILE | PATH | <tag>] Add an expanded token or fixed tag to every output line.\n"
            "  [-C native | si | customary] Convert units in decoded output.\n"
            "  [-T <seconds>] Specify number of seconds to run, also 12:34 or 1h23m45s\n"
            "  [-E hop | quit] Hop/Quit after outputting successful event(s)\n"
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
        term_help_printf("\t\t= Supported device protocols =\n");
        for (i = 0; i < num_devices; i++) {
            disabledc = devices[i].disabled ? '*' : ' ';
            if (devices[i].disabled <= 2) // if not hidden
                fprintf(stderr, "    [%02u]%c %s\n", i + 1, disabledc, devices[i].name);
        }
        fprintf(stderr, "\n* Disabled by default, use -R n or -G\n");
    }
    exit(exit_code);
}

static void help_device(void)
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

static void help_output(void)
{
    term_help_printf(
            "\t\t= Output format option =\n"
            "  [-F kv|json|csv|mqtt|influx|syslog|null] Produce decoded output in given format.\n"
            "\tWithout this option the default is KV output. Use \"-F null\" to remove the default.\n"
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
            "\tSpecify InfluxDB 2.0 server with e.g. -F \"influx://localhost:9999/api/v2/write?org=<org>&bucket=<bucket>,token=<authtoken>\"\n"
            "\tSpecify InfluxDB 1.x server with e.g. -F \"influx://localhost:8086/write?db=<db>&p=<password>&u=<user>\"\n"
            "\t  Additional parameter -M time:unix:usec:utc for correct timestamps in InfluxDB recommended\n"
            "\tSpecify host/port for syslog with e.g. -F syslog:127.0.0.1:1514\n");
    exit(0);
}

static void help_meta(void)
{
    term_help_printf(
            "\t\t= Meta information option =\n"
            "  [-M time[:<options>]|protocol|level|stats|bits|newmodel] Add various metadata to every output line.\n"
            "\tUse \"time\" to add current date and time meta data (preset for live inputs).\n"
            "\tUse \"time:rel\" to add sample position meta data (preset for read-file and stdin).\n"
            "\tUse \"time:unix\" to show the seconds since unix epoch as time meta data.\n"
            "\tUse \"time:iso\" to show the time with ISO-8601 format (YYYY-MM-DD\"T\"hh:mm:ss).\n"
            "\tUse \"time:off\" to remove time meta data.\n"
            "\tUse \"time:usec\" to add microseconds to date time meta data.\n"
            "\tUse \"time:tz\" to output time with timezone offset.\n"
            "\tUse \"time:utc\" to output time in UTC.\n"
            "\t\t(this may also be accomplished by invocation with TZ environment variable set).\n"
            "\t\t\"usec\" and \"utc\" can be combined with other options, eg. \"time:unix:utc:usec\".\n"
            "\tUse \"protocol\" / \"noprotocol\" to output the decoder protocol number meta data.\n"
            "\tUse \"level\" to add Modulation, Frequency, RSSI, SNR, and Noise meta data.\n"
            "\tUse \"stats[:[<level>][:<interval>]]\" to report statistics (default: 600 seconds).\n"
            "\t  level 0: no report, 1: report successful devices, 2: report active devices, 3: report all\n"
            "\tUse \"bits\" to add bit representation to code outputs (for debug).\n"
            "\nNote:"
            "\tUse \"newmodel\" to transition to new model keys. This will become the default someday.\n"
            "\tA table of changes and discussion is at https://github.com/merbanan/rtl_433/pull/986.\n\n");
    exit(0);
}

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

static void help_write(void)
{
    term_help_printf(
            "\t\t= Write file option =\n"
            "  [-w <filename>] Save data stream to output file (a '-' dumps samples to stdout)\n"
            "  [-W <filename>] Save data stream to output file, overwrite existing file\n"
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
    if (n_samples * 2 * demod->sample_size != len) {
        fprintf(stderr, "Sample buffer length not aligned to sample size!\n");
    }
    if (!n_samples) {
        fprintf(stderr, "Sample buffer too short!\n");
        return; // keep the watchdog timer running
    }

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
    /* Select the correct fsk pulse detector */
    unsigned fpdm = cfg->fsk_pulse_detect_mode;
    if (cfg->fsk_pulse_detect_mode == FSK_PULSE_DETECT_AUTO) {
        if (cfg->frequency[cfg->frequency_index] > FSK_PULSE_DETECTOR_LIMIT)
            fpdm = FSK_PULSE_DETECT_NEW;
        else
            fpdm = FSK_PULSE_DETECT_OLD;
    }

    if (demod->enable_FM_demod) {
        if (demod->sample_size == 1) { // CU8
            baseband_demod_FM(iq_buf, demod->buf.fm, n_samples, &demod->demod_FM_state, fpdm);
        } else { // CS16
            baseband_demod_FM_cs16((int16_t *)iq_buf, demod->buf.fm, n_samples, &demod->demod_FM_state, fpdm);
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
        int package_type = PULSE_DATA_OOK;  // Just to get us started
        for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
            file_info_t const *dumper = *iter;
            if (dumper->format == U8_LOGIC) {
                memset(demod->u8_buf, 0, n_samples);
                break;
            }
        }
        while (package_type) {
            int p_events = 0; // Sensor events successfully detected per package
            package_type = pulse_detect_package(demod->pulse_detect, demod->am_buf, demod->buf.fm, n_samples, demod->level_limit, cfg->samp_rate, cfg->input_pos, &demod->pulse_data, &demod->fsk_pulse_data, fpdm);
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

                if (cfg->verbosity > 2) pulse_data_print(&demod->pulse_data);
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

                if (cfg->verbosity > 2) pulse_data_print(&demod->fsk_pulse_data);
                if (demod->analyze_pulses && (cfg->grab_mode <= 1 || (cfg->grab_mode == 2 && p_events == 0) || (cfg->grab_mode == 3 && p_events > 0)) ) {
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

    if (cfg->after_successful_events_flag && (d_events > 0)) {
        if (cfg->after_successful_events_flag == 1) {
            cfg->do_exit = 1;
        }
        cfg->do_exit_async = 1;
#ifndef _WIN32
        alarm(0); // cancel the watchdog timer
#endif
        sdr_stop(cfg->dev);
    }

    time_t rawtime;
    time(&rawtime);
    // choose hop_index as frequency_index, if there are too few hop_times use the last one
    int hop_index = cfg->hop_times > cfg->frequency_index ? cfg->frequency_index : cfg->hop_times - 1;
    if (cfg->hop_times > 0 && cfg->frequencies > 1
            && difftime(rawtime, cfg->hop_start_time) > cfg->hop_time[hop_index]) {
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
    if (cfg->stats_now || (cfg->report_stats && cfg->stats_interval && rawtime >= cfg->stats_time)) {
        event_occurred_handler(cfg, create_report_data(cfg, cfg->stats_now ? 3 : cfg->report_stats));
        flush_report_data(cfg);
        if (rawtime >= cfg->stats_time)
            cfg->stats_time += cfg->stats_interval;
        if (cfg->stats_now)
            cfg->stats_now--;
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

#define OPTSTRING "hVvqDc:x:z:p:a:AI:S:m:M:r:w:W:l:d:t:f:H:g:s:b:n:R:X:F:K:C:T:UG:y:E:Y:"

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
        {"fsk_pulse_detect_mode", 'Y'},
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
        if (cfg->frequencies < MAX_FREQS) {
            uint32_t sr = atouint32_metric(arg, "-f: ");
            /* If the frequency is above 800MHz sample at 1MS/s */
            if ((sr > FSK_PULSE_DETECTOR_LIMIT) && (cfg->samp_rate == DEFAULT_SAMPLE_RATE))
                cfg->samp_rate = 1000000;
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

        cfg->gain_str = arg;
        break;
    case 'G':
        if (atobv(arg, 1) == 4) {
            fprintf(stderr, "\n\tUse -G for testing only. Enable protocols with -R if you really need them.\n\n");
            cfg->no_default_devices = 1;
            register_all_protocols(cfg, 1);
        }
        else {
            fprintf(stderr, "\n\tUse -G for testing only. Enable with -G 4 if you really mean it.\n\n");
            exit(1);
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
        if (atobv(arg, 1) == 4 && !cfg->demod->am_analyze) {
            cfg->demod->am_analyze = am_analyze_create();
        }
        else {
            fprintf(stderr, "\n\tUse -a for testing only. Enable with -a 4 if you really mean it.\n\n");
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
        else if (!strcasecmp(arg, "bits"))
            cfg->verbose_bits = 1;
        else if (!strcasecmp(arg, "description"))
            cfg->report_description = 1;
        else if (!strcasecmp(arg, "newmodel"))
            cfg->new_model_keys = 1;
        else if (!strcasecmp(arg, "oldmodel"))
            cfg->new_model_keys = 0;
        else if (!strncasecmp(arg, "stats", 5)) {
            // there also should be options to set wether to flush on report
            char *p = arg_param(arg);
            cfg->report_stats = atoiv(p, 1);
            cfg->stats_interval = atoiv(arg_param(p), 600);
            time(&cfg->stats_time);
            cfg->stats_time += cfg->stats_interval;
        }
        else
            cfg->report_meta = atobv(arg, 1);
        break;
    case 'D':
        fprintf(stderr, "debug option (-D) is deprecated. See -v to increase verbosity\n");
        break;
    case 'z':
        if (!arg)
            usage(1);
        if (cfg->demod->am_analyze)
            cfg->demod->am_analyze->override_short = atoi(arg);
        break;
    case 'x':
        if (!arg)
            usage(1);
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
        else if (strncmp(arg, "mqtt", 4) == 0) {
            add_mqtt_output(cfg, arg_param(arg));
        }
        else if (strncmp(arg, "http", 4) == 0
                || strncmp(arg, "influx", 6) == 0) {
            add_influx_output(cfg, arg);
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
    case 'Y':
        if (!arg)
            usage(1);
        if (strcmp(arg, "auto") == 0) {
            cfg->fsk_pulse_detect_mode = FSK_PULSE_DETECT_AUTO;
        }
        else if (strcmp(arg, "classic") == 0) {
            cfg->fsk_pulse_detect_mode = FSK_PULSE_DETECT_OLD;
        }
        else if (strcmp(arg, "minmax") == 0) {
            cfg->fsk_pulse_detect_mode = FSK_PULSE_DETECT_NEW;
        }
        else {
            fprintf(stderr, "Invalid FSK pulse detector mode: %s\n", arg);
            usage(1);
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

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
    if (CTRL_C_EVENT == signum) {
        fprintf(stderr, "Signal caught, exiting!\n");
        g_cfg.do_exit = 1;
        sdr_stop(g_cfg.dev);
        return TRUE;
    }
    else if (CTRL_BREAK_EVENT == signum) {
        fprintf(stderr, "CTRL-BREAK detected, hopping to next frequency (-f). Use CTRL-C to quit.\n");
        g_cfg.do_exit_async = 1;
        sdr_stop(g_cfg.dev);
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
        g_cfg.do_exit_async = 1;
        sdr_stop(g_cfg.dev);
        return;
    }
    else if (signum == SIGALRM) {
        fprintf(stderr, "Async read stalled, exiting!\n");
    }
    else {
        fprintf(stderr, "Signal caught, exiting!\n");
    }
    g_cfg.do_exit = 1;
    sdr_stop(g_cfg.dev);
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
    r_cfg_t *cfg = &g_cfg;

    print_version(); // always print the version info

    r_init_cfg(cfg);

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    demod = cfg->demod;

    demod->pulse_detect = pulse_detect_create();

    /* initialize tables */
    baseband_init();

    r_device r_devices[] = {
#define DECL(name) name,
            DEVICES
#undef DECL
            };

    cfg->num_r_devices = sizeof(r_devices) / sizeof(*r_devices);
    for (i = 0; i < cfg->num_r_devices; i++) {
        r_devices[i].protocol_num = i + 1;
    }
    cfg->devices = r_devices;

    // if there is no explicit conf file option look for default conf files
    if (!hasopt('c', argc, argv, OPTSTRING)) {
        parse_conf_try_default_files(cfg);
    }

    parse_conf_args(cfg, argc, argv);

    // warn if still using old model keys
    if (!cfg->new_model_keys) {
        fprintf(stderr,
                "\n\tConsider using \"-M newmodel\" to transition to new model keys. This will become the default someday.\n"
                "\tA table of changes and discussion is at https://github.com/merbanan/rtl_433/pull/986.\n\n");
    }

    // add all remaining positional arguments as input files
    while (argc > optind) {
        add_infile(cfg, argv[optind++]);
    }

    if (demod->am_analyze) {
        demod->am_analyze->level_limit = &demod->level_limit;
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

    // register default decoders if nothing is configured
    if (!cfg->no_default_devices) {
        register_all_protocols(cfg, 0); // register all defaults
    } else {
        update_protocols(cfg);
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
            demod->r_devs.len, cfg->num_r_devices);

    if (!cfg->verbosity) {
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
                fprintf(stderr, " %u", num);
            else
                fprintf(stderr, " %u-%u", num, r_dev->protocol_num);
        }
        fprintf(stderr, " ]");
    }
    fprintf(stderr, "\n");

    start_outputs(cfg, well_known_output_fields(cfg));

    if (cfg->out_block_size < MINIMAL_BUF_LENGTH ||
            cfg->out_block_size > MAXIMAL_BUF_LENGTH) {
        fprintf(stderr,
                "Output block size wrong value, falling back to default\n");
        fprintf(stderr,
                "Minimal length: %d\n", MINIMAL_BUF_LENGTH);
        fprintf(stderr,
                "Maximal length: %d\n", MAXIMAL_BUF_LENGTH);
        cfg->out_block_size = DEFAULT_BUF_LENGTH;
    }

    // Special case for streaming test data
    if (cfg->test_data && (!strcasecmp(cfg->test_data, "-") || *cfg->test_data == '@')) {
        FILE *fp;
        char line[INPUT_LINE_MAX];

        if (*cfg->test_data == '@') {
            fprintf(stderr, "Reading test data from \"%s\"\n", &cfg->test_data[1]);
            fp = fopen(&cfg->test_data[1], "r");
        } else {
            fprintf(stderr, "Reading test data from stdin\n");
            fp = stdin;
        }
        if (!fp) {
            fprintf(stderr, "Failed to open %s\n", cfg->test_data);
            exit(1);
        }

        while (fgets(line, INPUT_LINE_MAX, fp)) {
            if (cfg->verbosity)
                fprintf(stderr, "Processing test data \"%s\"...\n", line);
            r = 0;
            // test a single decoder?
            if (*line == '[') {
                char *e = NULL;
                unsigned d = (unsigned)strtol(&line[1], &e, 10);
                if (!e || *e != ']') {
                    fprintf(stderr, "Bad protocol number %.5s.\n", line);
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
                    fprintf(stderr, "Unknown protocol number %u.\n", d);
                    exit(1);
                }
                if (cfg->verbosity)
                    fprintf(stderr, "Verifying test data with device %s.\n", r_dev->name);
                r += pulse_demod_string(e, r_dev);
                continue;
            }
            // otherwise test all decoders
            for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
                r_device *r_dev = *iter;
                if (cfg->verbosity)
                    fprintf(stderr, "Verifying test data with device %s.\n", r_dev->name);
                r += pulse_demod_string(line, r_dev);
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
        for (void **iter = demod->r_devs.elems; iter && *iter; ++iter) {
            r_device *r_dev = *iter;
            if (cfg->verbosity)
                fprintf(stderr, "Verifying test data with device %s.\n", r_dev->name);
            r += pulse_demod_string(cfg->test_data, r_dev);
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

            parse_file_info(cfg->in_filename, &demod->load_info);
            if (strcmp(demod->load_info.path, "-") == 0) { /* read samples from stdin */
                in_file = stdin;
                cfg->in_filename = "<stdin>";
            } else {
                in_file = fopen(demod->load_info.path, "rb");
                if (!in_file) {
                    fprintf(stderr, "Opening file: %s failed!\n", cfg->in_filename);
                    break;
                }
            }
            fprintf(stderr, "Test mode active. Reading samples from file: %s\n", cfg->in_filename);  // Essential information (not quiet)
            if (demod->load_info.format == CU8_IQ
                    || demod->load_info.format == S16_AM
                    || demod->load_info.format == S16_FM) {
                demod->sample_size = sizeof(uint8_t); // CU8, AM, FM
            } else if (demod->load_info.format == CS16_IQ
                    || demod->load_info.format == CF32_IQ) {
                demod->sample_size = sizeof(int16_t); // CF32, CS16
            } else if (demod->load_info.format == PULSE_OOK) {
                // ignore
            } else {
                fprintf(stderr, "Input format invalid: %s\n", file_info_string(&demod->load_info));
                break;
            }
            if (cfg->verbosity) {
                fprintf(stderr, "Input format: %s\n", file_info_string(&demod->load_info));
            }
            demod->sample_file_pos = 0.0;

            // special case for pulse data file-inputs
            if (demod->load_info.format == PULSE_OOK) {
                while (!cfg->do_exit) {
                    pulse_data_load(in_file, &demod->pulse_data, cfg->samp_rate);
                    if (!demod->pulse_data.num_pulses)
                        break;

                    if (demod->pulse_data.fsk_f2_est) {
                        run_fsk_demods(&demod->r_devs, &demod->pulse_data);
                    }
                    else {
                        int p_events = run_ook_demods(&demod->r_devs, &demod->pulse_data);
                        if (cfg->verbosity > 2)
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
            do {
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
                }
                if (n_read == 0) break;  // sdr_callback() will Segmentation Fault with len=0
                demod->sample_file_pos = ((float)n_blocks * DEFAULT_BUF_LENGTH + n_read) / cfg->samp_rate / 2 / demod->sample_size;
                n_blocks++; // this assumes n_read == DEFAULT_BUF_LENGTH
                sdr_callback(test_mode_buf, n_read, cfg);
            } while (n_read != 0 && !cfg->do_exit);

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
            demod->sample_file_pos = ((float)n_blocks + 1) * DEFAULT_BUF_LENGTH / cfg->samp_rate / 2 / demod->sample_size;
            sdr_callback(test_mode_buf, DEFAULT_BUF_LENGTH, cfg);

            //Always classify a signal at the end of the file
            if (demod->am_analyze)
                am_analyze_classify(demod->am_analyze);
            if (cfg->verbosity) {
                fprintf(stderr, "Test mode file issued %d packets\n", n_blocks);
            }

            if (in_file != stdin)
                fclose(in_file = stdin);
        }

        free(test_mode_buf);
        free(test_mode_float_buf);
        r_free_cfg(cfg);
        exit(0);
    }

    // Normal case, no test data, no in files
    r = sdr_open(&cfg->dev, &demod->sample_size, cfg->dev_query, cfg->verbosity);
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
    sigaction(SIGUSR1, &sigact, NULL);
    sigaction(SIGINFO, &sigact, NULL);
#else
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)sighandler, TRUE);
#endif
    /* Set the sample rate */
    r = sdr_set_sample_rate(cfg->dev, cfg->samp_rate, 1); // always verbose

    if (cfg->verbosity || demod->level_limit)
        fprintf(stderr, "Bit detection level set to %d%s.\n", demod->level_limit, (demod->level_limit ? "" : " (Auto)"));

    r = sdr_apply_settings(cfg->dev, cfg->settings_str, 1); // always verbose for soapy

    /* Enable automatic gain if gain_str empty (or 0 for RTL-SDR), set manual gain otherwise */
    r = sdr_set_tuner_gain(cfg->dev, cfg->gain_str, 1); // always verbose

    if (cfg->ppm_error)
        r = sdr_set_freq_correction(cfg->dev, cfg->ppm_error, 1); // always verbose

    /* Reset endpoint before we start reading from it (mandatory) */
    r = sdr_reset(cfg->dev, cfg->verbosity);
    if (r < 0)
        fprintf(stderr, "WARNING: Failed to reset buffers.\n");
    r = sdr_activate(cfg->dev);

    if (cfg->frequencies == 0) {
        cfg->frequency[0] = DEFAULT_FREQUENCY;
        cfg->frequencies = 1;
    }
    if (cfg->frequencies > 1 && cfg->hop_times == 0) {
        cfg->hop_time[cfg->hop_times++] = DEFAULT_HOP_TIME;
    }
    if (cfg->verbosity) {
        fprintf(stderr, "Reading samples in async mode...\n");
    }
    if (cfg->duration > 0) {
        time(&cfg->stop_time);
        cfg->stop_time += cfg->duration;
    }

    uint32_t samp_rate = cfg->samp_rate;
    while (!cfg->do_exit) {
        time(&cfg->hop_start_time);

        /* Set the cfg->frequency */
        cfg->center_frequency = cfg->frequency[cfg->frequency_index];
        r = sdr_set_center_freq(cfg->dev, cfg->center_frequency, 1); // always verbose

        if (samp_rate != cfg->samp_rate) {
            r = sdr_set_sample_rate(cfg->dev, cfg->samp_rate, 1); // always verbose
            update_protocols(cfg);
            samp_rate = cfg->samp_rate;
        }

#ifndef _WIN32
        signal(SIGALRM, sighandler);
        alarm(3); // require callback to run every 3 second, abort otherwise
#endif
        r = sdr_start(cfg->dev, sdr_callback, (void *)cfg,
                DEFAULT_ASYNC_BUF_NUMBER, cfg->out_block_size);
        if (r < 0) {
            fprintf(stderr, "WARNING: async read failed (%i).\n", r);
            break;
        }
#ifndef _WIN32
        alarm(0); // cancel the watchdog timer
#endif
        cfg->do_exit_async = 0;
        cfg->frequency_index = (cfg->frequency_index + 1) % cfg->frequencies;
    }

    if (cfg->report_stats > 0) {
        event_occurred_handler(cfg, create_report_data(cfg, cfg->report_stats));
        flush_report_data(cfg);
    }

    if (!cfg->do_exit)
        fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

    r_free_cfg(cfg);

    return r >= 0 ? r : -r;
}
