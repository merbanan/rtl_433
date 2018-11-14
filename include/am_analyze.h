/**
 * AM signal analyzer
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdint.h>

typedef struct {
    int32_t *level_limit;
    int override_short;
    int override_long;
    uint32_t *frequency;
    uint32_t *samp_rate;

    /* Signal grabber variables */
    int signal_grabber;
    char *sg_buf;
    unsigned sg_size;
    unsigned sg_index;
    unsigned sg_len;

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
    unsigned signal_end;
    unsigned signal_pulse_counter;
    unsigned signal_pulse_data[4000][3];
} am_analyze_t;

am_analyze_t *am_analyze_create(void);

void am_analyze_free(am_analyze_t *a);

void am_analyze_enable_grabber(am_analyze_t *a, unsigned size);

void am_analyze_add(am_analyze_t *a, unsigned char *iq_buf, uint32_t len);

void am_analyze_reset(am_analyze_t *a);

void am_analyze(am_analyze_t *a, int16_t *buf, uint32_t len, int debug_output);

void am_analyze_classify(am_analyze_t *aa);

void signal_grabber_write(am_analyze_t *a, unsigned signal_start, unsigned signal_end, unsigned i);
