/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2026 Christian W. Zuckschwerdt <zany@triq.net>

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

#include "r_flow.h"
#include "rtl_433.h"
#include "r_private.h"
#include "r_device.h"
#include "r_api.h"
#include "baseband.h"
#include "pulse_analyzer.h"
#include "pulse_detect.h"
#include "pulse_detect_fsk.h"
#include "pulse_slicer.h"
#include "data.h"
#include "raw_output.h"
#include "r_util.h"
#include "am_analyze.h"
#include "logger.h"
#include "fatal.h"

static void calc_rssi_snr(struct dm_state const *demod, pulse_data_t *pulse_data)
{
    float ook_high_estimate      = pulse_data->ook_high_estimate > 0 ? pulse_data->ook_high_estimate : 1;
    float ook_low_estimate       = pulse_data->ook_low_estimate > 0 ? pulse_data->ook_low_estimate : 1;
    int const OOK_MAX_HIGH_LEVEL = DB_TO_AMP(0); // Maximum estimate for high level (-0 dB)
    float ook_max_estimate       = ook_high_estimate < OOK_MAX_HIGH_LEVEL ? ook_high_estimate : OOK_MAX_HIGH_LEVEL;
    float asnr                   = ook_max_estimate / ook_low_estimate;
    float foffs1                 = (float)pulse_data->fsk_f1_est / INT16_MAX * demod->samp_rate / 2.0f;
    float foffs2                 = (float)pulse_data->fsk_f2_est / INT16_MAX * demod->samp_rate / 2.0f;
    pulse_data->freq1_hz         = (foffs1 + demod->center_frequency);
    pulse_data->freq2_hz         = (foffs2 + demod->center_frequency);
    pulse_data->centerfreq_hz    = demod->center_frequency;
    pulse_data->depth_bits       = demod->sample_size * 4;
    // NOTE: for (CU8) amplitude is 10x (because it's squares)
    if (demod->sample_size == 2 && !demod->use_mag_est) {                    // amplitude (CU8)
        pulse_data->range_db = 42.1442f;                                     // 10*log10f(16384.0f) == 20*log10f(128.0f)
        pulse_data->rssi_db  = 10.0f * log10f(ook_high_estimate) - 42.1442f; // 10*log10f(16384.0f)
        pulse_data->noise_db = 10.0f * log10f(ook_low_estimate) - 42.1442f;  // 10*log10f(16384.0f)
        pulse_data->snr_db   = 10.0f * log10f(asnr);
    }
    else {                               // magnitude (CU8, CS16)
        pulse_data->range_db = 84.2884f; // 20*log10f(16384.0f)
        // lowest (scaled x128) reading at  8 bit is -20*log10(128) = -42.1442 (eff. -36 dB)
        // lowest (scaled div2) reading at 12 bit is -20*log10(1024) = -60.2060 (eff. -54 dB)
        // lowest (scaled div2) reading at 16 bit is -20*log10(16384) = -84.2884 (eff. -78 dB)
        pulse_data->rssi_db  = 20.0f * log10f(ook_high_estimate) - 84.2884f; // 20*log10f(16384.0f)
        pulse_data->noise_db = 20.0f * log10f(ook_low_estimate) - 84.2884f;  // 20*log10f(16384.0f)
        pulse_data->snr_db   = 20.0f * log10f(asnr);
    }
}

static unsigned fsk_detector_order(struct dm_state const *demod, unsigned order[FSK_PULSE_DETECTOR_COUNT])
{
    if (demod->fsk_pulse_detect_mode == FSK_PULSE_DETECT_OLD) {
        order[0] = FSK_PULSE_DETECTOR_CLASSIC;
        return 1;
    }
    if (demod->fsk_pulse_detect_mode == FSK_PULSE_DETECT_NEW) {
        order[0] = FSK_PULSE_DETECTOR_MINMAX;
        order[1] = FSK_PULSE_DETECTOR_HYSTERESIS;
        return 2;
    }
    if (demod->fsk_pulse_detect_mode == FSK_PULSE_DETECT_HYSTERESIS) {
        order[0] = FSK_PULSE_DETECTOR_HYSTERESIS;
        return 1;
    }
    if (demod->fsk_pulse_detect_mode == FSK_PULSE_DETECT_MEDIAN) {
        order[0] = FSK_PULSE_DETECTOR_MEDIAN;
        return 1;
    }
    if (demod->center_frequency > FSK_PULSE_DETECTOR_LIMIT) {
        order[0] = FSK_PULSE_DETECTOR_MINMAX;
        order[1] = FSK_PULSE_DETECTOR_CLASSIC;
        order[2] = FSK_PULSE_DETECTOR_HYSTERESIS;
        order[3] = FSK_PULSE_DETECTOR_MEDIAN;
    }
    else {
        order[0] = FSK_PULSE_DETECTOR_CLASSIC;
        order[1] = FSK_PULSE_DETECTOR_MINMAX;
        order[2] = FSK_PULSE_DETECTOR_HYSTERESIS;
        order[3] = FSK_PULSE_DETECTOR_MEDIAN;
    }
    return FSK_PULSE_DETECTOR_COUNT;
}

static char const *fsk_detector_name(unsigned detector)
{
    static char const *const names[FSK_PULSE_DETECTOR_COUNT] = {
            "classic", "minmax", "hysteresis", "median"};
    return detector < FSK_PULSE_DETECTOR_COUNT ? names[detector] : "unknown";
}

static unsigned fsk_primary_detector(struct dm_state const *demod)
{
    unsigned order[FSK_PULSE_DETECTOR_COUNT];
    fsk_detector_order(demod, order);
    return order[0];
}

/**
Flush the SDR IQ data frame processing, e.g. on the end of a input file.

@return Count of successful decoding events
*/
int flush_sdr_flow(r_cfg_t *cfg)
{
    return push_sdr_flow(cfg, NULL, 0);
}

/**
Reset the SDR IQ data frame processing, e.g. on a new input file.
*/
void reset_sdr_flow(r_cfg_t *cfg)
{
    struct dm_state *demod = cfg->demod;

    get_time_now(&demod->now);

    demod->frame_start_ago   = 0;
    demod->frame_end_ago     = 0;
    demod->frame_event_count = 0;
    demod->frame_quality     = 0;

    demod->min_level_auto = 0.0f;
    demod->noise_level    = 0.0f;

    baseband_low_pass_filter_reset(&demod->lowpass_filter_state);
    baseband_demod_FM_reset(&demod->demod_FM_state);

    pulse_detect_reset(demod->pulse_detect);
    lora_fft_demod_reset(demod->lora_fft_demod);
}

/**
Push an IQ data frame to the SDR IQ data frame processing.

@return Count of successful decoding events
*/
int push_sdr_flow(r_cfg_t *cfg, unsigned char *iq_buf, uint32_t len)
{
    //fprintf(stderr, "push_sdr_flow... %u\n", len);
    struct dm_state *demod = cfg->demod;
    char time_str[LOCAL_TIME_BUFLEN];

    if (!demod) {
        // might happen when the demod closed and we get a last data frame
        return 0; // ignore the data
    }

    unsigned long n_samples = len / demod->sample_size;
    if (n_samples * demod->sample_size != len) {
        print_log(LOG_WARNING, __func__, "Sample buffer length not aligned to sample size!");
    }

    int process_frame = 1;

    // Process new frame data if available
    if (len) {

    // Feed data to all raw outputs (e.g. rtl_tcp)
    // do this here and not in sdr_handler so realtime replay can use rtl_tcp output
    for (void **iter = demod->raw_handler->elems; iter && *iter; ++iter) {
        raw_output_t *output = *iter;
        raw_output_frame(output, iq_buf, len);
    }

    // save last frame time to see if a new second started
    time_t last_frame_sec = demod->now.tv_sec;
    get_time_now(&demod->now);

    // age the frame position if there is one
    if (demod->frame_start_ago) {
        demod->frame_start_ago += n_samples;
    }
    if (demod->frame_end_ago) {
        demod->frame_end_ago += n_samples;
    }

    // Feed data to the sample grabber if available
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

    // Squelch silent frames
    //fprintf(stderr, "noise level: %.1f dB current: %.1f dB min level: %.1f dB\n", demod->noise_level, avg_db, demod->min_level_auto);
    if (demod->min_level_auto == 0.0f) {
        demod->min_level_auto = demod->min_level;
    }
    if (demod->noise_level == 0.0f) {
        demod->noise_level = demod->min_level_auto - 3.0f;
    }
    int noise_only = avg_db < demod->noise_level + 3.0f; // or demod->min_level_auto?
    // always process frames if loader, dumper, or analyzers are in use, otherwise skip silent frames
    process_frame = demod->squelch_offset <= 0 || !noise_only || demod->load_info.format || demod->analyze_pulses || demod->dumper.len || demod->samp_grab;
    demod->total_frames_count += 1;
    if (noise_only) {
        demod->total_frames_squelch += 1;
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
    if (demod->report_noise && last_frame_sec != demod->now.tv_sec && demod->now.tv_sec % demod->report_noise == 0) {
        print_logf(LOG_WARNING, "Auto Level", "Current %s level %.1f dB, estimated noise %.1f dB",
                noise_only ? "noise" : "signal", avg_db, demod->noise_level);
    }

    // Run AM filters
    if (process_frame) {
        baseband_low_pass_filter(&demod->lowpass_filter_state, demod->buf.temp, demod->am_buf, n_samples);
    }

    // FM demodulation
    if (demod->enable_FM_demod && process_frame) {
        // Preserve the legacy frequency-dependent filter while running the pulse detectors in parallel.
        int const prefer_minmax = demod->fsk_pulse_detect_mode == FSK_PULSE_DETECT_NEW
                || demod->fsk_pulse_detect_mode == FSK_PULSE_DETECT_HYSTERESIS
                || demod->fsk_pulse_detect_mode == FSK_PULSE_DETECT_MEDIAN
                || (demod->fsk_pulse_detect_mode == FSK_PULSE_DETECT_AUTO
                        && demod->center_frequency > FSK_PULSE_DETECTOR_LIMIT);
        float fm_low_pass = demod->fm_low_pass != 0.0f ? demod->fm_low_pass : (prefer_minmax ? 0.2f : 0.1f);
        if (demod->sample_size == 2) { // CU8
            baseband_demod_FM(&demod->demod_FM_state, iq_buf, demod->buf.fm, n_samples, demod->samp_rate, fm_low_pass);
        } else { // CS16
            baseband_demod_FM_cs16(&demod->demod_FM_state, (int16_t *)iq_buf, demod->buf.fm, n_samples, demod->samp_rate, fm_low_pass);
        }
    }

    // Handle special input formats
    if (demod->load_info.format == S16_AM) { // The IQ buffer is really AM demodulated data
        if (len > sizeof(demod->am_buf)) {
            FATAL("Buffer too small");
        }
        memcpy(demod->am_buf, iq_buf, len);
    } else if (demod->load_info.format == S16_FM) { // The IQ buffer is really FM demodulated data
        // we would need AM for the envelope too
        if (len > sizeof(demod->buf.fm)) {
            FATAL("Buffer too small");
        }
        memcpy(demod->buf.fm, iq_buf, len);
    }

    }

    // Run a pulse discriminator and pass packages to all configured slicers
    int d_events = 0; // Sensor events successfully detected
    int const iq_input = !demod->load_info.format
            || demod->load_info.format == CU8_IQ
            || demod->load_info.format == CS8_IQ
            || demod->load_info.format == CS16_IQ
            || demod->load_info.format == CF32_IQ;
    /* LoRa analysis is CPU intensive and is opt-in through -Y lora-fft. */
    if (demod->lora_fft_demod && len && process_frame && iq_input) {
        unsigned spreading_factor;
        uint32_t bandwidth;
        unsigned sync_word;
        if (get_lora_params(&demod->r_devs, &spreading_factor, &bandwidth, &sync_word)) {
            lora_packet_t packets[8];
            int packet_count;
            if (demod->sample_size == 2) {
                packet_count = lora_fft_demod_process_cu8(demod->lora_fft_demod, iq_buf,
                        n_samples, demod->input_pos, demod->samp_rate,
                        spreading_factor, bandwidth, sync_word, packets, 8);
            }
            else {
                packet_count = lora_fft_demod_process_cs16(demod->lora_fft_demod,
                        (int16_t const *)iq_buf, n_samples, demod->input_pos,
                        demod->samp_rate, spreading_factor, bandwidth,
                        sync_word, packets, 8);
            }

            if (packet_count < 0) {
                print_logf(LOG_WARNING, "LoRa FFT", "No supported SF/BW candidates at %u samples per second",
                        demod->samp_rate);
            }
            for (int i = 0; i < packet_count; ++i) {
                uint64_t const buffer_end = demod->input_pos + n_samples;
                demod->pulse_data.start_ago = packets[i].start_offset < buffer_end
                        ? (unsigned)(buffer_end - packets[i].start_offset) : 0;
                d_events += run_lora_demods(&demod->r_devs,
                        packets[i].payload, packets[i].payload_len,
                        packets[i].spreading_factor, packets[i].bandwidth,
                        packets[i].sync_word);
            }
        }
    }
    if (demod->r_devs.len || demod->analyze_pulses || demod->dumper.len || demod->samp_grab) {
        // Detect a package and loop through demodulators with pulse data
        int package_type = PULSE_DATA_OOK;  // Just to get us started
        // Initialize all U8 logic buffers
        for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
            file_info_t const *dumper = *iter;
            if (dumper->format == U8_LOGIC) {
                memset(demod->u8_buf, 0, n_samples);
                break;
            }
        }
        while (package_type && process_frame) {
            int p_events = 0; // Sensor events successfully detected per package
            package_type = pulse_detect_package(demod->pulse_detect, demod->am_buf, demod->buf.fm, n_samples,
                    demod->samp_rate, demod->input_pos, &demod->pulse_data, demod->fsk_pulse_data_all,
                    demod->fsk_pulse_detect_mode, fsk_primary_detector(demod));
            if (package_type) {
                // new package: set a first frame start if we are not tracking one already
                if (!demod->frame_start_ago) {
                    demod->frame_start_ago = demod->pulse_data.start_ago;
                }
                // always update the last frame end
                demod->frame_end_ago = demod->pulse_data.end_ago;
            }
            if (package_type == PULSE_DATA_OOK) {
                calc_rssi_snr(demod, &demod->pulse_data);
                if (demod->analyze_pulses) {
                    fprintf(stderr, "Detected OOK package\t%s\n", time_pos_str(cfg, demod->pulse_data.start_ago, time_str));
                }

                p_events += run_ook_demods(&demod->r_devs, &demod->pulse_data);
                demod->total_frames_ook += 1;
                demod->total_frames_events += p_events > 0;
                demod->frames_ook += 1;
                demod->frames_events += p_events > 0;

                // Dump pulse data for this complete package
                for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
                    file_info_t const *dumper = *iter;
                    if (dumper->format == VCD_LOGIC) {
                        pulse_data_print_vcd(dumper->file, &demod->pulse_data, '\'');
                    }
                    if (dumper->format == U8_LOGIC) {
                        pulse_data_dump_raw(demod->u8_buf, n_samples, demod->input_pos, &demod->pulse_data, 0x02);
                    }
                    if (dumper->format == PULSE_OOK) {
                        pulse_data_dump(dumper->file, &demod->pulse_data);
                    }
                }

                if (demod->verbosity >= LOG_TRACE) {
                    pulse_data_print(&demod->pulse_data);
                }
                if (demod->raw_mode == 1 || (demod->raw_mode == 2 && p_events == 0) || (demod->raw_mode == 3 && p_events > 0)) {
                    data_t *data = pulse_data_print_data(&demod->pulse_data);
                    event_occurred_handler(cfg, data);
                }
                if (demod->analyze_pulses && (demod->grab_mode <= 1 || (demod->grab_mode == 2 && p_events == 0) || (demod->grab_mode == 3 && p_events > 0))) {
                    r_device device = {.log_fn = log_device_handler, .output_ctx = cfg};
                    pulse_analyzer(&demod->pulse_data, package_type, &device);
                }
                if (demod->grab_mode == 4 && p_events == 0) {
                    r_device device = {.log_fn = log_device_handler, .output_ctx = cfg};
                    int p_quality   = pulse_analyzer_check(&demod->pulse_data, package_type, &device);
                    demod->frame_quality = p_quality > demod->frame_quality ? p_quality : demod->frame_quality;
                }
            }
            else if (package_type == PULSE_DATA_FSK) {
                unsigned order[FSK_PULSE_DETECTOR_COUNT];
                unsigned const order_len = fsk_detector_order(demod, order);
                int first_detector = -1;
                for (unsigned i = 0; i < order_len && p_events == 0; ++i) {
                    unsigned const detector = order[i];
                    pulse_data_t *candidate = &demod->fsk_pulse_data_all[detector];
                    if (candidate->num_pulses <= PD_MIN_PULSES) {
                        continue;
                    }
                    if (demod->verbosity >= LOG_TRACE) {
                        fprintf(stderr, "Trying FSK detector %s (%u pulses)\n",
                                fsk_detector_name(detector), candidate->num_pulses);
                    }
                    calc_rssi_snr(demod, candidate);
                    if (first_detector < 0) {
                        first_detector = detector;
                    }
                    demod->fsk_pulse_data = *candidate;
                    p_events += run_fsk_demods(&demod->r_devs, &demod->fsk_pulse_data);
                }
                if (p_events == 0 && first_detector >= 0) {
                    demod->fsk_pulse_data = demod->fsk_pulse_data_all[first_detector];
                }
                if (demod->analyze_pulses) {
                    fprintf(stderr, "Detected FSK package\t%s\n", time_pos_str(cfg, demod->fsk_pulse_data.start_ago, time_str));
                }

                demod->total_frames_fsk += 1;
                demod->total_frames_events += p_events > 0;
                demod->frames_fsk += 1;
                demod->frames_events += p_events > 0;

                // Dump pulse data for this complete package
                for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
                    file_info_t const *dumper = *iter;
                    if (dumper->format == VCD_LOGIC) {
                        pulse_data_print_vcd(dumper->file, &demod->fsk_pulse_data, '"');
                    }
                    if (dumper->format == U8_LOGIC) {
                        pulse_data_dump_raw(demod->u8_buf, n_samples, demod->input_pos, &demod->fsk_pulse_data, 0x04);
                    }
                    if (dumper->format == PULSE_OOK) {
                        pulse_data_dump(dumper->file, &demod->fsk_pulse_data);
                    }
                }

                if (demod->verbosity >= LOG_TRACE) {
                    pulse_data_print(&demod->fsk_pulse_data);
                }
                if (demod->raw_mode == 1 || (demod->raw_mode == 2 && p_events == 0) || (demod->raw_mode == 3 && p_events > 0)) {
                    data_t *data = pulse_data_print_data(&demod->fsk_pulse_data);
                    event_occurred_handler(cfg, data);
                }
                if (demod->analyze_pulses && (demod->grab_mode <= 1 || (demod->grab_mode == 2 && p_events == 0) || (demod->grab_mode == 3 && p_events > 0))) {
                    r_device device = {.log_fn = log_device_handler, .output_ctx = cfg};
                    pulse_analyzer(&demod->fsk_pulse_data, package_type, &device);
                }
                if (demod->grab_mode == 4 && p_events == 0) {
                    r_device device = {.log_fn = log_device_handler, .output_ctx = cfg};
                    int p_quality   = pulse_analyzer_check(&demod->fsk_pulse_data, package_type, &device);
                    demod->frame_quality = p_quality > demod->frame_quality ? p_quality : demod->frame_quality;
                }
            } // if (package_type == ...
            d_events += p_events;
        } // while (package_type)...

        // add event counter to the frames currently tracked
        demod->frame_event_count += d_events;

        // end frame tracking if older than a whole buffer
        if (demod->frame_start_ago && demod->frame_end_ago > n_samples) {
            if (demod->samp_grab) {
                if (demod->grab_mode == 1
                        || (demod->grab_mode == 2 && demod->frame_event_count == 0)
                        || (demod->grab_mode == 3 && demod->frame_event_count > 0)
                        || (demod->grab_mode == 4 && demod->frame_event_count == 0 && demod->frame_quality > 0)) {
                    unsigned frame_pad = n_samples / 8; // this could also be a fixed value, e.g. 10000 samples
                    unsigned start_padded = demod->frame_start_ago + frame_pad;
                    unsigned end_padded = demod->frame_end_ago - frame_pad;
                    unsigned len_padded = start_padded - end_padded;
                    samp_grab_write(demod->samp_grab, len_padded, end_padded);
                }
            }
            demod->frame_start_ago   = 0;
            demod->frame_event_count = 0;
            demod->frame_quality     = 0;
        }

        // Dump partial pulse data, might overlap with the last complete package
        for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
            file_info_t const *dumper = *iter;
            if (dumper->format == U8_LOGIC) {
                unsigned order[FSK_PULSE_DETECTOR_COUNT];
                fsk_detector_order(demod, order);
                pulse_data_dump_raw(demod->u8_buf, n_samples, demod->input_pos, &demod->pulse_data, 0x02);
                pulse_data_dump_raw(demod->u8_buf, n_samples, demod->input_pos,
                        &demod->fsk_pulse_data_all[order[0]], 0x04);
                break;
            }
        }
    }

    // End processing if no new frame data
    if (!len) {
        return d_events;
    }

    // Run the AM analyzer (deprecated)
    if (demod->am_analyze) {
        am_analyze(demod->am_analyze, demod->am_buf, n_samples, demod->verbosity >= LOG_INFO, NULL);
    }

    // Save data to all dumpers (expect logic dumpers)
    for (void **iter = demod->dumper.elems; iter && *iter; ++iter) {
        file_info_t const *dumper = *iter;
        if (!dumper->file
                || dumper->format == VCD_LOGIC
                || dumper->format == PULSE_OOK) {
            continue;
        }
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
            if (demod->sample_size == 2) {
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = (iq_buf[n * 2] - 128) * (1.0f / 0x80); // scale from Q0.7
            }
            else {
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = ((int16_t *)iq_buf)[n * 2] * (1.0f / 0x8000); // scale from Q0.15
            }
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == F32_Q) {
            if (demod->sample_size == 2) {
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = (iq_buf[n * 2 + 1] - 128) * (1.0f / 0x80); // scale from Q0.7
            }
            else {
                for (unsigned long n = 0; n < n_samples; ++n)
                    demod->f32_buf[n] = ((int16_t *)iq_buf)[n * 2 + 1] * (1.0f / 0x8000); // scale from Q0.15
            }
            out_buf = (uint8_t *)demod->f32_buf;
            out_len = n_samples * sizeof(float);
        }
        else if (dumper->format == U8_LOGIC) { // state data
            out_buf = demod->u8_buf;
            out_len = n_samples;
        }

        if (fwrite(out_buf, 1, out_len, dumper->file) != out_len) {
            print_log(LOG_ERROR, __func__, "Short write, samples lost, exiting!");
            d_events = -1; // report error, handler should exit
        }
    }

    demod->input_pos += n_samples;

    return d_events;
}
