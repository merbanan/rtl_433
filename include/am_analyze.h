/** @file
    AM signal analyzer.

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_AM_ANALYZE_H_
#define INCLUDE_AM_ANALYZE_H_

#include <stdint.h>

#define PULSE_DATA_SIZE 4000 /* maximum number of pulses */

typedef struct am_analyze {
    int32_t *level_limit;
    int override_short;
    int override_long;
    uint32_t *frequency;
    uint32_t *samp_rate;
    int *sample_size;

    /* state */
    unsigned counter;
    unsigned print;
    unsigned print2;
    unsigned pulses_found;
    unsigned prev_pulse_start;
    unsigned pulse_start;
    unsigned pulse_end;
    unsigned pulse_avg;
    unsigned signal_start;
    unsigned signal_pulse_counter;
    unsigned signal_pulse_data[4000][3];
} am_analyze_t;

am_analyze_t *am_analyze_create(void);

void am_analyze_free(am_analyze_t *a);

void am_analyze_skip(am_analyze_t *a, unsigned n_samples);

void am_analyze(am_analyze_t *a, int16_t *am_buf, unsigned n_samples, int debug_output, samp_grab_t *g);

void am_analyze_classify(am_analyze_t *aa);

#endif /* INCLUDE_AM_ANALYZE_H_ */
