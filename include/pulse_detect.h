/** @file
    Pulse detection functions.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_PULSE_DETECT_H_
#define INCLUDE_PULSE_DETECT_H_

#include <stdint.h>
#include <stdio.h>
#include "r_util.h"

#define PD_MAX_PULSES 1200      // Maximum number of pulses before forcing End Of Package
#define PD_MIN_PULSES 16        // Minimum number of pulses before declaring a proper package
#define PD_MIN_PULSE_SAMPLES 10 // Minimum number of samples in a pulse for proper detection
#define PD_MIN_GAP_MS 10        // Minimum gap size in milliseconds to exceed to declare End Of Package
#define PD_MAX_GAP_MS 100       // Maximum gap size in milliseconds to exceed to declare End Of Package
#define PD_MAX_GAP_RATIO 10     // Ratio gap/pulse width to exceed to declare End Of Package (heuristic)
#define PD_MAX_PULSE_MS 100     // Pulse width in ms to exceed to declare End Of Package (e.g. for non OOK packages)

/// Data for a compact representation of generic pulse train.
typedef struct pulse_data {
    uint64_t offset;            ///< Offset to first pulse in number of samples from start of stream.
    uint32_t sample_rate;       ///< Sample rate the pulses are recorded with.
    unsigned start_ago;         ///< Start of first pulse in number of samples ago.
    unsigned end_ago;           ///< End of last pulse in number of samples ago.
    unsigned int num_pulses;
    int pulse[PD_MAX_PULSES];   ///< Width of pulses (high) in number of samples.
    int gap[PD_MAX_PULSES];     ///< Width of gaps between pulses (low) in number of samples.
    int ook_low_estimate;       ///< Estimate for the OOK low level (base noise level) at beginning of package.
    int ook_high_estimate;      ///< Estimate for the OOK high level at end of package.
    int fsk_f1_est;             ///< Estimate for the F1 frequency for FSK.
    int fsk_f2_est;             ///< Estimate for the F2 frequency for FSK.
    float freq1_hz;
    float freq2_hz;
    float rssi_db;
    float snr_db;
    float noise_db;
} pulse_data_t;

typedef struct pulse_detect pulse_detect_t;

/// Clear the content of a pulse_data_t structure.
void pulse_data_clear(pulse_data_t *data);

/// Print the content of a pulse_data_t structure (for debug).
void pulse_data_print(pulse_data_t const *data);

/// Dump the content of a pulse_data_t structure as raw binary.
void pulse_data_dump_raw(uint8_t *buf, unsigned len, uint64_t buf_offset, pulse_data_t const *data, uint8_t bits);

/// Print a header for the VCD format.
void pulse_data_print_vcd_header(FILE *file, uint32_t sample_rate);

/// Print the content of a pulse_data_t structure in VCD format.
void pulse_data_print_vcd(FILE *file, pulse_data_t const *data, int ch_id);

/// Read the next pulse_data_t structure from OOK text.
void pulse_data_load(FILE *file, pulse_data_t *data);

/// Print a header for the OOK text format.
void pulse_data_print_pulse_header(FILE *file);

/// Print the content of a pulse_data_t structure as OOK text.
void pulse_data_dump(FILE *file, pulse_data_t *data);

pulse_detect_t *pulse_detect_create(void);

void pulse_detect_free(pulse_detect_t *pulse_detect);

/// Demodulate On/Off Keying (OOK) and Frequency Shift Keying (FSK) from an envelope signal.
///
/// Function is stateful and can be called with chunks of input data.
/// @param envelope_data: Samples with amplitude envelope of carrier
/// @param fm_data: Samples with frequency offset from center frequency
/// @param len: Number of samples in input buffers
/// @param samp_rate: Sample rate in samples per second
/// @param[in,out] pulses: Will return a pulse_data_t structure
/// @param[in,out] fsk_pulses: Will return a pulse_data_t structure for FSK demodulated data
/// @return 0 if all input sample data is processed
/// @return 1 if OOK package is detected (but all sample data is still not completely processed)
/// @return 2 if FSK package is detected (but all sample data is still not completely processed)
int pulse_detect_package(pulse_detect_t *pulse_detect, int16_t const *envelope_data, int16_t const *fm_data, int len, int16_t level_limit, uint32_t samp_rate, uint64_t sample_offset, pulse_data_t *pulses, pulse_data_t *fsk_pulses);

/// Analyze and print result.
void pulse_analyzer(pulse_data_t *data);


#endif /* INCLUDE_PULSE_DETECT_H_ */
