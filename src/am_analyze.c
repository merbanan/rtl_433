/** @file
    AM signal analyzer.

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bitbuffer.h"
#include "samp_grab.h"
#include "fatal.h"

#include "am_analyze.h"

#include "log.h"
#define LOG_MODULE "am_analyze"

#define FRAME_END_MIN 50000 /* minimum sample count to detect frame end */
#define FRAME_PAD 10000 /* number of samples to pad both frame start and end */

am_analyze_t *am_analyze_create(void)
{
    am_analyze_t *a;
    a = calloc(1, sizeof(am_analyze_t));
    if (!a)
        WARN_CALLOC("am_analyze_create()");
    return a; // NOTE: returns NULL on alloc failure.
}

void am_analyze_free(am_analyze_t *a)
{
    free(a);
}

void am_analyze_skip(am_analyze_t *a, unsigned n_samples)
{
    a->counter += n_samples;
    a->signal_start = 0;
}

void am_analyze(am_analyze_t *a, int16_t *am_buf, unsigned n_samples, int debug_output, samp_grab_t *g)
{
    unsigned int i;
    int threshold = (a->level_limit ? a->level_limit : 8000);  // Does not support auto level. Use old default instead.

    for (i = 0; i < n_samples; i++) {
        if (am_buf[i] > threshold) {
            if (!a->signal_start)
                a->signal_start = a->counter;
            if (a->print) {
                a->pulses_found++;
                a->pulse_start = a->counter;
                a->signal_pulse_data[a->signal_pulse_counter][0] = a->counter;
                a->signal_pulse_data[a->signal_pulse_counter][1] = -1;
                a->signal_pulse_data[a->signal_pulse_counter][2] = -1;
                if (debug_output) {
                    fprintf(stderr, "pulse_distance %u\n", a->counter - a->pulse_end);
                    fprintf(stderr, "pulse_start distance %u\n", a->pulse_start - a->prev_pulse_start);
                    fprintf(stderr, "pulse_start[%u] found at sample %u, value = %d\n", a->pulses_found, a->counter, am_buf[i]);
                }
                a->prev_pulse_start = a->pulse_start;
                a->print = 0;
                a->print2 = 1;
            }
        }
        a->counter++;
        if (am_buf[i] < threshold) {
            if (a->print2) {
                a->pulse_avg += a->counter - a->pulse_start;
                if (debug_output) {
                    fprintf(stderr, "pulse_end  [%u] found at sample %u, pulse length = %u, pulse avg length = %u\n",
                            a->pulses_found, a->counter, a->counter - a->pulse_start, (a->pulses_found) ? (a->pulse_avg / a->pulses_found) : 0);
                }
                a->pulse_end = a->counter;
                a->print2 = 0;
                a->signal_pulse_data[a->signal_pulse_counter][1] = a->counter;
                a->signal_pulse_data[a->signal_pulse_counter][2] = a->counter - a->pulse_start;
                a->signal_pulse_counter++;
                if (a->signal_pulse_counter >= PULSE_DATA_SIZE) {
                    a->signal_pulse_counter = 0;
                    fprintf(stderr, "Too many pulses detected, probably bad input data or input parameters\n");
                    return;
                }
            }
            a->print = 1;
            if (a->signal_start && (a->pulse_end + FRAME_END_MIN < a->counter)) {
                unsigned padded_start = a->signal_start - FRAME_PAD;
                unsigned padded_end   = a->counter - FRAME_END_MIN + FRAME_PAD;
                unsigned padded_len   = padded_end - padded_start;
                fprintf(stderr, "*** signal_start = %u, signal_end = %u, signal_len = %u, pulses_found = %u\n",
                        padded_start, padded_end, padded_len, a->pulses_found);

                am_analyze_classify(a); // clears signal_pulse_data
                a->pulses_found = 0;

                if (g) {
                    samp_grab_write(g, padded_len, n_samples - i - 1);
                }
                a->signal_start = 0;
            }
        }
    }
}


void am_analyze_classify(am_analyze_t *aa)
{
    unsigned int i, k, max = 0, min = 1000000, t;
    unsigned int delta, p_limit;
    unsigned int a[3], b[2], a_cnt[3], a_new[3];
    unsigned int signal_distance_data[PULSE_DATA_SIZE] = {0};
    bitbuffer_t bits = {0};
    unsigned int signal_type;

    if (!aa->signal_pulse_data[0][0])
        return;

    for (i = 0; i < aa->signal_pulse_counter; i++) {
        if (aa->signal_pulse_data[i][0] > 0) {
            //fprintf(stderr, "[%03d] s: %d\t  e:\t %d\t l:%d\n",
            //i, aa->signal_pulse_data[i][0], aa->signal_pulse_data[i][1],
            //aa->signal_pulse_data[i][2]);
            if (aa->signal_pulse_data[i][2] > max)
                max = aa->signal_pulse_data[i][2];
            if (aa->signal_pulse_data[i][2] <= min)
                min = aa->signal_pulse_data[i][2];
        }
    }
    t = (max + min) / 2;
    //fprintf(stderr, "\n\nMax: %d, Min: %d  t:%d\n", max, min, t);

    delta = (max - min)*(max - min);

    //TODO use Lloyd-Max quantizer instead
    k = 1;
    while ((k < 10) && (delta > 0)) {
        unsigned min_new = 0;
        unsigned count_min = 0;
        unsigned max_new = 0;
        unsigned count_max = 0;

        for (i = 0; i < aa->signal_pulse_counter; i++) {
            if (aa->signal_pulse_data[i][0] > 0) {
                if (aa->signal_pulse_data[i][2] < t) {
                    min_new = min_new + aa->signal_pulse_data[i][2];
                    count_min++;
                } else {
                    max_new = max_new + aa->signal_pulse_data[i][2];
                    count_max++;
                }
            }
        }
        if (count_min != 0 && count_max != 0) {
            min_new = min_new / count_min;
            max_new = max_new / count_max;
        }

        delta = (min - min_new)*(min - min_new) + (max - max_new)*(max - max_new);
        min = min_new;
        max = max_new;
        t = (min + max) / 2;

        fprintf(stderr, "Iteration %u. t: %u    min: %u (%u)    max: %u (%u)    delta %u\n", k, t, min, count_min, max, count_max, delta);
        k++;
    }

    for (i = 0; i < aa->signal_pulse_counter; i++) {
        if (aa->signal_pulse_data[i][0] > 0) {
            //fprintf(stderr, "%d\n", aa->signal_pulse_data[i][1]);
        }
    }
    /* 50% decision limit */
    if (min != 0 && max / min > 1) {
        fprintf(stderr, "Pulse coding: Short pulse length %u - Long pulse length %u\n", min, max);
        signal_type = 2;
    } else {
        fprintf(stderr, "Distance coding: Pulse length %u\n", (min + max) / 2);
        signal_type = 1;
    }
    p_limit = (max + min) / 2;

    /* Initial guesses */
    a[0] = 1000000;
    a[2] = 0;
    for (i = 1; i < aa->signal_pulse_counter; i++) {
        if (aa->signal_pulse_data[i][0] > 0) {
            //               fprintf(stderr, "[%03d] s: %d\t  e:\t %d\t l:%d\t  d:%d\n",
            //               i, aa->signal_pulse_data[i][0], aa->signal_pulse_data[i][1],
            //               aa->signal_pulse_data[i][2], aa->signal_pulse_data[i][0]-aa->signal_pulse_data[i-1][1]);
            signal_distance_data[i - 1] = aa->signal_pulse_data[i][0] - aa->signal_pulse_data[i - 1][1];
            if (signal_distance_data[i - 1] > a[2])
                a[2] = signal_distance_data[i - 1];
            if (signal_distance_data[i - 1] <= a[0])
                a[0] = signal_distance_data[i - 1];
        }
    }
    min = a[0];
    max = a[2];
    a[1] = (a[0] + a[2]) / 2;
    //    for (i=0 ; i<1 ; i++) {
    //        b[i] = (a[i]+a[i+1])/2;
    //    }
    b[0] = (a[0] + a[1]) / 2;
    b[1] = (a[1] + a[2]) / 2;
    //     fprintf(stderr, "a[0]: %d\t a[1]: %d\t a[2]: %d\t\n",a[0],a[1],a[2]);
    //     fprintf(stderr, "b[0]: %d\t b[1]: %d\n",b[0],b[1]);

    k = 1;
    delta = 10000000;
    while ((k < 10) && (delta > 0)) {
        for (i = 0; i < 3; i++) {
            a_new[i] = 0;
            a_cnt[i] = 0;
        }

        for (i = 0; i < aa->signal_pulse_counter; i++) {
            if (signal_distance_data[i] > 0) {
                if (signal_distance_data[i] < b[0]) {
                    a_new[0] += signal_distance_data[i];
                    a_cnt[0]++;
                } else if (signal_distance_data[i] < b[1] && signal_distance_data[i] >= b[0]) {
                    a_new[1] += signal_distance_data[i];
                    a_cnt[1]++;
                } else if (signal_distance_data[i] >= b[1]) {
                    a_new[2] += signal_distance_data[i];
                    a_cnt[2]++;
                }
            }
        }

        //         fprintf(stderr, "Iteration %d.", k);
        delta = 0;
        for (i = 0; i < 3; i++) {
            if (a_cnt[i])
                a_new[i] /= a_cnt[i];
            delta += (a[i] - a_new[i])*(a[i] - a_new[i]);
            //             fprintf(stderr, "\ta[%d]: %d (%d)", i, a_new[i], a[i]);
            a[i] = a_new[i];
        }
        //         fprintf(stderr, " delta %d\n", delta);

        if (a[0] < min) {
            a[0] = min;
            //             fprintf(stderr, "Fixing a[0] = %d\n", min);
        }
        if (a[2] > max) {
            a[0] = max;
            //             fprintf(stderr, "Fixing a[2] = %d\n", max);
        }
        //         if (a[1] == 0) {
        //             a[1] = (a[2]+a[0])/2;
        //             fprintf(stderr, "Fixing a[1] = %d\n", a[1]);
        //         }

        //         fprintf(stderr, "Iteration %d.", k);
        for (i = 0; i < 2; i++) {
            //             fprintf(stderr, "\tb[%d]: (%d) ", i, b[i]);
            b[i] = (a[i] + a[i + 1]) / 2;
            //             fprintf(stderr, "%d  ", b[i]);
        }
        //         fprintf(stderr, "\n");
        k++;
    }

    if (aa->override_short) {
        p_limit = aa->override_short;
        a[0] = aa->override_short;
    }

    if (aa->override_long) {
        a[1] = aa->override_long;
    }

    fprintf(stderr, "\nShort distance: %u, long distance: %u, packet distance: %u\n", a[0], a[1], a[2]);
    fprintf(stderr, "\np_limit: %u\n", p_limit);

    bitbuffer_clear(&bits);
    if (signal_type == 1) {
        for (i = 0; i < aa->signal_pulse_counter; i++) {
            if (signal_distance_data[i] > 0) {
                if (signal_distance_data[i] < (a[0] + a[1]) / 2) {
                    //                     fprintf(stderr, "0 [%d] %d < %d\n",i, signal_distance_data[i], (a[0]+a[1])/2);
                    bitbuffer_add_bit(&bits, 0);
                } else if ((signal_distance_data[i] > (a[0] + a[1]) / 2) && (signal_distance_data[i] < (a[1] + a[2]) / 2)) {
                    //                     fprintf(stderr, "0 [%d] %d > %d\n",i, signal_distance_data[i], (a[0]+a[1])/2);
                    bitbuffer_add_bit(&bits, 1);
                } else if (signal_distance_data[i] > (a[1] + a[2]) / 2) {
                    //                     fprintf(stderr, "0 [%d] %d > %d\n",i, signal_distance_data[i], (a[1]+a[2])/2);
                    bitbuffer_add_row(&bits);
                }

            }

        }
        bitbuffer_print(&bits);
    }
    if (signal_type == 2) {
        for (i = 0; i < aa->signal_pulse_counter; i++) {
            if (aa->signal_pulse_data[i][2] > 0) {
                if (aa->signal_pulse_data[i][2] < p_limit) {
                    //                     fprintf(stderr, "0 [%d] %d < %d\n",i, aa->signal_pulse_data[i][2], p_limit);
                    bitbuffer_add_bit(&bits, 0);
                } else {
                    //                     fprintf(stderr, "1 [%d] %d > %d\n",i, aa->signal_pulse_data[i][2], p_limit);
                    bitbuffer_add_bit(&bits, 1);
                }
                if ((signal_distance_data[i] >= (a[1] + a[2]) / 2)) {
                    //                     fprintf(stderr, "\\n [%d] %d > %d\n",i, signal_distance_data[i], (a[1]+a[2])/2);
                    bitbuffer_add_row(&bits);
                }


            }
        }
        bitbuffer_print(&bits);
    }

    // clear signal_pulse_data
    aa->signal_pulse_counter = 0;
}
