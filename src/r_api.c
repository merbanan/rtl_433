/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "r_api.h"
#include "rtl_433.h"
#include "r_private.h"
#include "r_device.h"
#include "pulse_demod.h"
#include "pulse_detect_fsk.h"
#include "sdr.h"
#include "data.h"
#include "list.h"
#include "optparse.h"
#include "output_mqtt.h"
#include "output_influx.h"
#include "compat_time.h"
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

/* general */

void r_init_cfg(r_cfg_t *cfg)
{
    cfg->out_block_size  = DEFAULT_BUF_LENGTH;
    cfg->samp_rate       = DEFAULT_SAMPLE_RATE;
    cfg->conversion_mode = CONVERT_NATIVE;
    cfg->fsk_pulse_detect_mode = FSK_PULSE_DETECT_AUTO;

    list_ensure_size(&cfg->in_files, 100);
    list_ensure_size(&cfg->output_handler, 16);

    cfg->demod = calloc(1, sizeof(*cfg->demod));
    if (!cfg->demod)
        FATAL_CALLOC("r_init_cfg()");

    cfg->demod->level_limit = DEFAULT_LEVEL_LIMIT;

    list_ensure_size(&cfg->demod->r_devs, 100);
    list_ensure_size(&cfg->demod->dumper, 32);
}

r_cfg_t *r_create_cfg(void)
{
    r_cfg_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg)
        FATAL_CALLOC("r_create_cfg()");

    r_init_cfg(cfg);

    return cfg;
}

void r_free_cfg(r_cfg_t *cfg)
{
    if (cfg->dev) {
        sdr_deactivate(cfg->dev);
        sdr_close(cfg->dev);
    }

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

/* device decoder protocols */

void update_protocol(r_cfg_t *cfg, r_device *r_dev)
{
    float samples_per_us = cfg->samp_rate / 1.0e6;

    r_dev->f_short_width = r_dev->short_width > 0.0 ? 1.0 / (r_dev->short_width * samples_per_us) : 0;
    r_dev->f_long_width  = r_dev->long_width > 0.0 ? 1.0 / (r_dev->long_width * samples_per_us) : 0;
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

void register_protocol(r_cfg_t *cfg, r_device *r_dev, char *arg)
{
    r_device *p;
    if (r_dev->create_fn) {
        p = r_dev->create_fn(arg);
    }
    else {
        if (arg && *arg) {
            fprintf(stderr, "Protocol [%u] \"%s\" does not take arguments \"%s\"!\n", r_dev->protocol_num, r_dev->name, arg);
        }
        p  = malloc(sizeof(*p));
        if (!p)
            FATAL_CALLOC("register_protocol()");
        *p = *r_dev; // copy
    }

    update_protocol(cfg, p);

    p->output_fn  = data_acquired_handler;
    p->output_ctx = cfg;

    list_push(&cfg->demod->r_devs, p);

    if (cfg->verbosity) {
        fprintf(stderr, "Registering protocol [%u] \"%s\"\n", r_dev->protocol_num, r_dev->name);
    }
}

void free_protocol(r_device *r_dev)
{
    // free(r_dev->name);
    free(r_dev->decode_ctx);
    free(r_dev);
}

void unregister_protocol(r_cfg_t *cfg, r_device *r_dev)
{
    for (size_t i = 0; i < cfg->demod->r_devs.len; ++i) { // list might contain NULLs
        r_device *p = cfg->demod->r_devs.elems[i];
        if (!strcmp(p->name, r_dev->name)) {
            list_remove(&cfg->demod->r_devs, i, (list_elem_free_fn)free_protocol);
            i--; // so we don't skip the next elem now shifted down
        }
    }
}

void register_all_protocols(r_cfg_t *cfg, unsigned disabled)
{
    for (int i = 0; i < cfg->num_r_devices; i++) {
        // register all device protocols that are not disabled
        if (cfg->devices[i].disabled <= disabled) {
            register_protocol(cfg, &cfg->devices[i], NULL);
        }
    }
}

void update_protocols(r_cfg_t *cfg)
{
    for (void **iter = cfg->demod->r_devs.elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        update_protocol(cfg, r_dev);
    }
}

/* output helper */

void calc_rssi_snr(r_cfg_t *cfg, pulse_data_t *pulse_data)
{
    float ook_high_estimate = pulse_data->ook_high_estimate > 0 ? pulse_data->ook_high_estimate : 1;
    float ook_low_estimate = pulse_data->ook_low_estimate > 0 ? pulse_data->ook_low_estimate : 1;
    float asnr   = ook_high_estimate / ook_low_estimate;
    float foffs1 = (float)pulse_data->fsk_f1_est / INT16_MAX * cfg->samp_rate / 2.0;
    float foffs2 = (float)pulse_data->fsk_f2_est / INT16_MAX * cfg->samp_rate / 2.0;
    pulse_data->freq1_hz = (foffs1 + cfg->center_frequency);
    pulse_data->freq2_hz = (foffs2 + cfg->center_frequency);
    // NOTE: for (CU8) amplitude is 10x (because it's squares)
    if (cfg->demod->sample_size == 1) { // amplitude (CU8)
        pulse_data->rssi_db = 10.0f * log10f(ook_high_estimate) - 42.1442f; // 10*log10f(16384.0f)
        pulse_data->noise_db = 10.0f * log10f(ook_low_estimate) - 42.1442f; // 10*log10f(16384.0f)
        pulse_data->snr_db  = 10.0f * log10f(asnr);
    }
    else { // magnitude (CS16)
        pulse_data->rssi_db = 20.0f * log10f(ook_high_estimate) - 84.2884f; // 20*log10f(16384.0f)
        pulse_data->noise_db = 20.0f * log10f(ook_low_estimate) - 84.2884f; // 20*log10f(16384.0f)
        pulse_data->snr_db  = 20.0f * log10f(asnr);
    }
}

char *time_pos_str(r_cfg_t *cfg, unsigned samples_ago, char *buf)
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

        char const *format = NULL;
        if (cfg->report_time == REPORT_TIME_UNIX)
            format = "%s";
        else if (cfg->report_time == REPORT_TIME_ISO)
            format = "%Y-%m-%dT%H:%M:%S";

        if (cfg->report_time_hires)
            return usecs_time_str(buf, format, cfg->report_time_tz, &ago);
        else
            return format_time_str(buf, format, cfg->report_time_tz, ago.tv_sec);
    }
}

// well-known fields "time", "msg" and "codes" are used to output general decoder messages
// well-known field "bits" is only used when verbose bits (-M bits) is requested
// well-known field "tag" is only used when output tagging is requested
// well-known field "protocol" is only used when model protocol is requested
// well-known field "description" is only used when model description is requested
// well-known fields "mod", "freq", "freq1", "freq2", "rssi", "snr", "noise" are used by meta report option
static char const *well_known_default[15] = {0};
char const **well_known_output_fields(r_cfg_t *cfg)
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

/** Convert CSV keys according to selected conversion mode. Replacement is static but in-place. */
static char const **convert_csv_fields(r_cfg_t *cfg, char const **fields)
{
    if (cfg->new_model_keys) {
        for (char const **p = fields; *p; ++p) {
            if (!strcmp(*p, "battery")) *p = "battery_ok";
        }
    }

    if (cfg->conversion_mode == CONVERT_SI) {
        for (char const **p = fields; *p; ++p) {
            if (!strcmp(*p, "temperature_F")) *p = "temperature_C";
            else if (!strcmp(*p, "pressure_PSI")) *p = "pressure_kPa";
            else if (!strcmp(*p, "rain_in")) *p = "rain_mm";
            else if (!strcmp(*p, "rain_rate_in_h")) *p = "rain_rate_mm_h";
            else if (!strcmp(*p, "wind_avg_mi_h")) *p = "wind_avg_km_h";
            else if (!strcmp(*p, "wind_max_mi_h")) *p = "wind_max_km_h";
        }
    }

    if (cfg->conversion_mode == CONVERT_CUSTOMARY) {
        for (char const **p = fields; *p; ++p) {
            if (!strcmp(*p, "temperature_C")) *p = "temperature_F";
            else if (!strcmp(*p, "temperature_1_C")) *p = "temperature_1_F";
            else if (!strcmp(*p, "temperature_2_C")) *p = "temperature_2_F";
            else if (!strcmp(*p, "setpoint_C")) *p = "setpoint_F";
            else if (!strcmp(*p, "pressure_hPa")) *p = "pressure_inHg";
            else if (!strcmp(*p, "pressure_kPa")) *p = "pressure_PSI";
            else if (!strcmp(*p, "rain_mm")) *p = "rain_in";
            else if (!strcmp(*p, "rain_rate_mm_h")) *p = "rain_rate_in_h";
            else if (!strcmp(*p, "wind_avg_km_h")) *p = "wind_avg_mi_h";
            else if (!strcmp(*p, "wind_max_km_h")) *p = "wind_max_mi_h";
        }
    }
    return fields;
}

// find the fields output for CSV
char const **determine_csv_fields(r_cfg_t *cfg, char const **well_known, int *num_fields)
{
    list_t field_list = {0};
    list_ensure_size(&field_list, 100);

    // always add well-known fields
    list_push_all(&field_list, (void **)well_known);

    list_t *r_devs = &cfg->demod->r_devs;
    for (void **iter = r_devs->elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        if (r_dev->fields)
            list_push_all(&field_list, (void **)r_dev->fields);
        else
            fprintf(stderr, "rtl_433: warning: %u \"%s\" does not support CSV output\n",
                    r_dev->protocol_num, r_dev->name);
    }
    convert_csv_fields(cfg, (char const **)field_list.elems);

    if (num_fields)
        *num_fields = field_list.len;
    return (char const **)field_list.elems;
}

int run_ook_demods(list_t *r_devs, pulse_data_t *pulse_data)
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
            fprintf(stderr, "Unknown modulation %u in protocol!\n", r_dev->modulation);
        }
    }

    return p_events;
}

int run_fsk_demods(list_t *r_devs, pulse_data_t *fsk_pulse_data)
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
            fprintf(stderr, "Unknown modulation %u in protocol!\n", r_dev->modulation);
        }
    }

    return p_events;
}

/* handlers */

/** Pass the data structure to all output handlers. Frees data afterwards. */
void event_occurred_handler(r_cfg_t *cfg, data_t *data)
{
    // prepend "time" if requested
    if (cfg->report_time != REPORT_TIME_OFF) {
        char time_str[LOCAL_TIME_BUFLEN];
        time_pos_str(cfg, 0, time_str);
        data = data_prepend(data,
                "time", "", DATA_STRING, time_str,
                NULL);
    }

    for (size_t i = 0; i < cfg->output_handler.len; ++i) { // list might contain NULLs
        data_output_print(cfg->output_handler.elems[i], data);
    }
    data_free(data);
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
                if (!d->key)
                    FATAL_STRDUP("data_acquired_handler()");
                int ok = d->value.v_ptr && !strcmp(d->value.v_ptr, "OK");
                free(d->value.v_ptr);
                d->type = DATA_INT;
                d->value.v_int = ok;
                break;
            }
        }
    }

    if (cfg->conversion_mode == CONVERT_SI) {
        for (data_t *d = data; d; d = d->next) {
            // Convert double type fields ending in _F to _C
            if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_F")) {
                d->value.v_dbl = fahrenheit2celsius(d->value.v_dbl);
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
                d->value.v_dbl = mph2kmph(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_mph", "_kph");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "mi/h", "km/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _mi_h to _km_h
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mi_h")) {
                d->value.v_dbl = mph2kmph(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_mi_h", "_km_h");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "mi/h", "km/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _in to _mm
            else if ((d->type == DATA_DOUBLE) &&
                     (str_endswith(d->key, "_in") || str_endswith(d->key, "_inch"))) {
                d->value.v_dbl = inch2mm(d->value.v_dbl);
                char *new_label = str_replace(str_replace(d->key, "_inch", "_in"), "_in", "_mm");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "in", "mm");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _in_h to _mm_h
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_in_h")) {
                d->value.v_dbl = inch2mm(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_in_h", "_mm_h");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "in/h", "mm/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _inHg to _hPa
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_inHg")) {
                d->value.v_dbl = inhg2hpa(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_inHg", "_hPa");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "inHg", "hPa");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _PSI to _kPa
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_PSI")) {
                d->value.v_dbl = psi2kpa(d->value.v_dbl);
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
                d->value.v_dbl = celsius2fahrenheit(d->value.v_dbl);
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
                d->value.v_dbl = kmph2mph(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_kph", "_mph");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "km/h", "mi/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _km_h to _mi_h
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_km_h")) {
                d->value.v_dbl = kmph2mph(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_km_h", "_mi_h");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "km/h", "mi/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _mm to _inch
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mm")) {
                d->value.v_dbl = mm2inch(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_mm", "_in");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "mm", "in");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _mm_h to _in_h
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mm_h")) {
                d->value.v_dbl = mm2inch(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_mm_h", "_in_h");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "mm/h", "in/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _hPa to _inHg
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_hPa")) {
                d->value.v_dbl = hpa2inhg(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_hPa", "_inHg");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "hPa", "inHg");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _kPa to _PSI
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_kPa")) {
                d->value.v_dbl = kpa2psi(d->value.v_dbl);
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

// level 0: do not report (don't call this), 1: report successful devices, 2: report active devices, 3: report all
data_t *create_report_data(r_cfg_t *cfg, int level)
{
    list_t *r_devs = &cfg->demod->r_devs;
    data_t *data;
    list_t dev_data_list = {0};
    list_ensure_size(&dev_data_list, r_devs->len);

    for (void **iter = r_devs->elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        if (level <= 2 && r_dev->decode_events == 0)
            continue;
        if (level <= 1 && r_dev->decode_ok == 0)
            continue;
        if (level <= 0)
            continue;
        data = data_make(
                "device",       "", DATA_INT, r_dev->protocol_num,
                "name",         "", DATA_STRING, r_dev->name,
                "events",       "", DATA_INT, r_dev->decode_events,
                "ok",           "", DATA_INT, r_dev->decode_ok,
                "messages",     "", DATA_INT, r_dev->decode_messages,
                NULL);

        if (r_dev->decode_fails[-DECODE_FAIL_OTHER])
            data_append(data,
                    "fail_other",   "", DATA_INT, r_dev->decode_fails[-DECODE_FAIL_OTHER],
                    NULL);
        if (r_dev->decode_fails[-DECODE_ABORT_LENGTH])
            data_append(data,
                    "abort_length", "", DATA_INT, r_dev->decode_fails[-DECODE_ABORT_LENGTH],
                    NULL);
        if (r_dev->decode_fails[-DECODE_ABORT_EARLY])
            data_append(data,
                    "abort_early",  "", DATA_INT, r_dev->decode_fails[-DECODE_ABORT_EARLY],
                    NULL);
        if (r_dev->decode_fails[-DECODE_FAIL_MIC])
            data_append(data,
                    "fail_mic",     "", DATA_INT, r_dev->decode_fails[-DECODE_FAIL_MIC],
                    NULL);
        if (r_dev->decode_fails[-DECODE_FAIL_SANITY])
            data_append(data,
                    "fail_sanity",  "", DATA_INT, r_dev->decode_fails[-DECODE_FAIL_SANITY],
                    NULL);

        list_push(&dev_data_list, data);
    }

    data = data_make(
            "count",            "", DATA_INT, cfg->frames_count,
            "fsk",              "", DATA_INT, cfg->frames_fsk,
            "events",           "", DATA_INT, cfg->frames_events,
            NULL);

    data = data_make(
            "enabled",          "", DATA_INT, r_devs->len,
            "frames",           "", DATA_DATA, data,
            "stats",            "", DATA_ARRAY, data_array(dev_data_list.len, DATA_DATA, dev_data_list.elems),
            NULL);

    list_free_elems(&dev_data_list, NULL);
    return data;
}

void flush_report_data(r_cfg_t *cfg)
{
    list_t *r_devs = &cfg->demod->r_devs;

    cfg->frames_count = 0;
    cfg->frames_fsk = 0;
    cfg->frames_events = 0;

    for (void **iter = r_devs->elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;

        r_dev->decode_events = 0;
        r_dev->decode_ok = 0;
        r_dev->decode_messages = 0;
        r_dev->decode_fails[0] = 0;
        r_dev->decode_fails[1] = 0;
        r_dev->decode_fails[2] = 0;
        r_dev->decode_fails[3] = 0;
        r_dev->decode_fails[4] = 0;
    }
}

/* setup */

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

void add_json_output(r_cfg_t *cfg, char *param)
{
    list_push(&cfg->output_handler, data_output_json_create(fopen_output(param)));
}

void add_csv_output(r_cfg_t *cfg, char *param)
{
    list_push(&cfg->output_handler, data_output_csv_create(fopen_output(param)));
}

void start_outputs(r_cfg_t *cfg, char const **well_known)
{
    int num_output_fields;
    const char **output_fields = determine_csv_fields(cfg, well_known, &num_output_fields);

    for (size_t i = 0; i < cfg->output_handler.len; ++i) { // list might contain NULLs
        data_output_start(cfg->output_handler.elems[i], output_fields, num_output_fields);
    }

    free(output_fields);
}

void add_kv_output(r_cfg_t *cfg, char *param)
{
    list_push(&cfg->output_handler, data_output_kv_create(fopen_output(param)));
}

void add_mqtt_output(r_cfg_t *cfg, char *param)
{
    char *host = "localhost";
    char *port = "1883";
    char *opts = hostport_param(param, &host, &port);
    fprintf(stderr, "Publishing MQTT data to %s port %s\n", host, port);

    list_push(&cfg->output_handler, data_output_mqtt_create(host, port, opts, cfg->dev_query));
}

void add_influx_output(r_cfg_t *cfg, char *param)
{
    list_push(&cfg->output_handler, data_output_influx_create(param));
}

void add_syslog_output(r_cfg_t *cfg, char *param)
{
    char *host = "localhost";
    char *port = "514";
    hostport_param(param, &host, &port);
    fprintf(stderr, "Syslog UDP datagrams to %s port %s\n", host, port);

    list_push(&cfg->output_handler, data_output_syslog_create(host, port));
}

void add_null_output(r_cfg_t *cfg, char *param)
{
    (void)param;
    list_push(&cfg->output_handler, NULL);
}

void add_dumper(r_cfg_t *cfg, char const *spec, int overwrite)
{
    file_info_t *dumper = calloc(1, sizeof(*dumper));
    if (!dumper)
        FATAL_CALLOC("add_dumper()");
    list_push(&cfg->demod->dumper, dumper);

    parse_file_info(spec, dumper);
    if (strcmp(dumper->path, "-") == 0) { /* Write samples to stdout */
        dumper->file = stdout;
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
    }
    else {
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

void add_infile(r_cfg_t *cfg, char *in_file)
{
    list_push(&cfg->in_files, in_file);
}
