/** @file
    Pulse data structure and functions.

    Copyright (C) 2015 Tommy Vestermark
    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_PULSE_DATA_H_
#define INCLUDE_PULSE_DATA_H_

#include <stdint.h>
#include <stdio.h>
#include "data.h"

#define PD_MAX_PULSES        1200 // Maximum number of pulses before forcing End Of Package
#define PD_MIN_PULSES        16   // Minimum number of pulses before declaring a proper package
#define PD_MIN_PULSE_SAMPLES 10   // Minimum number of samples in a pulse for proper detection
#define PD_MIN_GAP_MS        10   // Minimum gap size in milliseconds to exceed to declare End Of Package
#define PD_MAX_GAP_MS        100  // Maximum gap size in milliseconds to exceed to declare End Of Package
#define PD_MAX_GAP_RATIO     10   // Ratio gap/pulse width to exceed to declare End Of Package (heuristic)
#define PD_MAX_PULSE_MS      100  // Pulse width in ms to exceed to declare End Of Package (e.g. for non OOK packages)

/// Data for a compact representation of generic pulse train.
typedef struct pulse_data {
    uint64_t offset;      ///< Offset to first pulse in number of samples from start of stream.
    uint32_t sample_rate; ///< Sample rate the pulses are recorded with.
    unsigned depth_bits;  ///< Sample depth in bits.
    unsigned start_ago;   ///< Start of first pulse in number of samples ago.
    unsigned end_ago;     ///< End of last pulse in number of samples ago.
    unsigned int num_pulses;
    int pulse[PD_MAX_PULSES]; ///< Width of pulses (high) in number of samples.
    int gap[PD_MAX_PULSES];   ///< Width of gaps between pulses (low) in number of samples.
    int ook_low_estimate;     ///< Estimate for the OOK low level (base noise level) at beginning of package.
    int ook_high_estimate;    ///< Estimate for the OOK high level at end of package.
    int fsk_f1_est;           ///< Estimate for the F1 frequency for FSK.
    int fsk_f2_est;           ///< Estimate for the F2 frequency for FSK.
    float freq1_hz;
    float freq2_hz;
    float centerfreq_hz;
    float range_db;
    float rssi_db;
    float snr_db;
    float noise_db;
} pulse_data_t;

/// Clear the content of a pulse_data_t structure.
void pulse_data_clear(pulse_data_t *data);

/// Shift out part of the data to make room for more.
void pulse_data_shift(pulse_data_t *data);

/// Print the content of a pulse_data_t structure (for debug).
void pulse_data_print(pulse_data_t const *data);

/// Dump the content of a pulse_data_t structure as raw binary.
void pulse_data_dump_raw(uint8_t *buf, unsigned len, uint64_t buf_offset, pulse_data_t const *data, uint8_t bits);

/// Print a header for the VCD format.
void pulse_data_print_vcd_header(FILE *file, uint32_t sample_rate);

/// Print the content of a pulse_data_t structure in VCD format.
void pulse_data_print_vcd(FILE *file, pulse_data_t const *data, int ch_id);

/// Read the next pulse_data_t structure from OOK text.
void pulse_data_load(FILE *file, pulse_data_t *data, uint32_t sample_rate);

/// Print a header for the OOK text format.
void pulse_data_print_pulse_header(FILE *file);

/// Print the content of a pulse_data_t structure as OOK text.
void pulse_data_dump(FILE *file, pulse_data_t const *data);

/// Print the content of a pulse_data_t structure as OOK json.
data_t *pulse_data_print_data(pulse_data_t const *data);

#endif /* INCLUDE_PULSE_DATA_H_ */
