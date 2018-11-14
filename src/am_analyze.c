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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bitbuffer.h"

#include "am_analyze.h"

am_analyze_t *am_analyze_create(void)
{
    am_analyze_t *a;
    a = calloc(1, sizeof(am_analyze_t));
    return a;
}

void am_analyze_free(am_analyze_t *a)
{
    if (a->sg_buf)
        free(a->sg_buf);
    free(a);
}

void am_analyze_enable_grabber(am_analyze_t *a, unsigned size)
{
    if (!a->sg_buf) {
        a->sg_buf = malloc(size);
        a->sg_size = size;
    }
    a->signal_grabber = 1;
}

void am_analyze_add(am_analyze_t *a, unsigned char *iq_buf, uint32_t len)
{
    if (a->signal_grabber) {
        //fprintf(stderr, "[%d] sg_index - len %d\n", a->sg_index, len );
        memcpy(&a->sg_buf[a->sg_index], iq_buf, len);
        a->sg_len = len;
        a->sg_index += len;
        if (a->sg_index + len > a->sg_size)
            a->sg_index = 0;
    }
}

void am_analyze_reset(am_analyze_t *a)
{
    a->signal_start = 0;
}

void am_analyze(am_analyze_t *a, int16_t *buf, uint32_t len, int debug_output) {
    unsigned int i;
    int32_t threshold = (*a->level_limit ? *a->level_limit : 8000);  // Does not support auto level. Use old default instead.

    for (i = 0; i < len; i++) {
        if (buf[i] > threshold) {
            if (!a->signal_start)
                a->signal_start = a->counter;
            if (a->print) {
                a->pulses_found++;
                a->pulse_start = a->counter;
                a->signal_pulse_data[a->signal_pulse_counter][0] = a->counter;
                a->signal_pulse_data[a->signal_pulse_counter][1] = -1;
                a->signal_pulse_data[a->signal_pulse_counter][2] = -1;
                if (debug_output) fprintf(stderr, "pulse_distance %d\n", a->counter - a->pulse_end);
                if (debug_output) fprintf(stderr, "a->pulse_start distance %d\n", a->pulse_start - a->prev_pulse_start);
                if (debug_output) fprintf(stderr, "a->pulse_start[%d] found at sample %d, value = %d\n", a->pulses_found, a->counter, buf[i]);
                a->prev_pulse_start = a->pulse_start;
                a->print = 0;
                a->print2 = 1;
            }
        }
        a->counter++;
        if (buf[i] < threshold) {
            if (a->print2) {
                a->pulse_avg += a->counter - a->pulse_start;
                if (debug_output) fprintf(stderr, "a->pulse_end  [%d] found at sample %d, pulse length = %d, pulse avg length = %d\n",
                        a->pulses_found, a->counter, a->counter - a->pulse_start, a->pulse_avg / a->pulses_found);
                a->pulse_end = a->counter;
                a->print2 = 0;
                a->signal_pulse_data[a->signal_pulse_counter][1] = a->counter;
                a->signal_pulse_data[a->signal_pulse_counter][2] = a->counter - a->pulse_start;
                a->signal_pulse_counter++;
                if (a->signal_pulse_counter >= 4000) {
                    a->signal_pulse_counter = 0;
                    fprintf(stderr, "To many pulses detected, probably bad input data or input parameters\n");
                    return;
                }
            }
            a->print = 1;
            if (a->signal_start && (a->pulse_end + 50000 < a->counter)) {
                a->signal_end = a->counter - 40000;
                fprintf(stderr, "*** a->signal_start = %d, a->signal_end = %d\n", a->signal_start - 10000, a->signal_end);
                fprintf(stderr, "signal_len = %d,  pulses = %d\n", a->signal_end - (a->signal_start - 10000), a->pulses_found);
                a->pulses_found = 0;
                am_analyze_classify(a);

                a->signal_pulse_counter = 0;
                if (a->sg_buf) {
                    signal_grabber_write(a, a->signal_start, a->signal_end, i);
                }
                a->signal_start = 0;
            }
        }
    }
}


void am_analyze_classify(am_analyze_t *aa) {
    unsigned int i, k, max = 0, min = 1000000, t;
    unsigned int delta, count_min, count_max, min_new, max_new, p_limit;
    unsigned int a[3], b[2], a_cnt[3], a_new[3];
    unsigned int signal_distance_data[4000] = {0};
    bitbuffer_t bits = {0};
    unsigned int signal_type;

    if (!aa->signal_pulse_data[0][0])
        return;

    for (i = 0; i < 1000; i++) {
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
        min_new = 0;
        count_min = 0;
        max_new = 0;
        count_max = 0;

        for (i = 0; i < 1000; i++) {
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

        fprintf(stderr, "Iteration %d. t: %d    min: %d (%d)    max: %d (%d)    delta %d\n", k, t, min, count_min, max, count_max, delta);
        k++;
    }

    for (i = 0; i < 1000; i++) {
        if (aa->signal_pulse_data[i][0] > 0) {
            //fprintf(stderr, "%d\n", aa->signal_pulse_data[i][1]);
        }
    }
    /* 50% decision limit */
    if (min != 0 && max / min > 1) {
        fprintf(stderr, "Pulse coding: Short pulse length %d - Long pulse length %d\n", min, max);
        signal_type = 2;
    } else {
        fprintf(stderr, "Distance coding: Pulse length %d\n", (min + max) / 2);
        signal_type = 1;
    }
    p_limit = (max + min) / 2;

    /* Initial guesses */
    a[0] = 1000000;
    a[2] = 0;
    for (i = 1; i < 1000; i++) {
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

        for (i = 0; i < 1000; i++) {
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

    fprintf(stderr, "\nShort distance: %d, long distance: %d, packet distance: %d\n", a[0], a[1], a[2]);
    fprintf(stderr, "\np_limit: %d\n", p_limit);

    bitbuffer_clear(&bits);
    if (signal_type == 1) {
        for (i = 0; i < 1000; i++) {
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
        for (i = 0; i < 1000; i++) {
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

    for (i = 0; i < 1000; i++) {
        aa->signal_pulse_data[i][0] = 0;
        aa->signal_pulse_data[i][1] = 0;
        aa->signal_pulse_data[i][2] = 0;
        signal_distance_data[i] = 0;
    }

}

void signal_grabber_write(am_analyze_t *a, unsigned signal_start, unsigned signal_end, unsigned i)
{
    if (!a->sg_buf)
        return;

    int start_pos, signal_bsize, wlen, wrest = 0, sg_idx, idx;
    char f_name[64] = {0};
    FILE *fp;

    while (1) {
        sprintf(f_name, "g%03d_%gM_%gk.cu8", a->signal_grabber, *a->frequency / 1000000.0, *a->samp_rate / 1000.0);
        a->signal_grabber++;
        if (access(f_name, F_OK) == -1) {
            break;
        }
    }

    signal_bsize = 2 * (a->signal_end - (signal_start - 10000));
    signal_bsize = (131072 - (signal_bsize % 131072)) + signal_bsize;
    sg_idx = a->sg_index - a->sg_len;
    if (sg_idx < 0)
        sg_idx = a->sg_size - a->sg_len;
    idx = (i - 40000) * 2;
    start_pos = sg_idx + idx - signal_bsize;
    fprintf(stderr, "signal_bsize = %d  -      sg_index = %d\n", signal_bsize, a->sg_index);
    fprintf(stderr, "start_pos    = %d  -   buffer_size = %d\n", start_pos, a->sg_size);
    if (signal_bsize > (int)a->sg_size)
        fprintf(stderr, "Signal bigger then buffer, signal = %d > buffer %d !!\n", signal_bsize, a->sg_size);

    if (start_pos < 0) {
        start_pos = a->sg_size + start_pos;
        fprintf(stderr, "restart_pos = %d\n", start_pos);
    }

    fprintf(stderr, "*** Saving signal to file %s\n", f_name);
    fp = fopen(f_name, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to open %s\n", f_name);
    }
    wlen = signal_bsize;
    if (start_pos + signal_bsize > (int)a->sg_size) {
        wlen  = a->sg_size - start_pos;
        wrest = signal_bsize - wlen;
    }
    fprintf(stderr, "*** Writing data from %d, len %d\n", start_pos, wlen);
    fwrite(&a->sg_buf[start_pos], 1, wlen, fp);

    if (wrest) {
        fprintf(stderr, "*** Writing data from %d, len %d\n", 0, wrest);
        fwrite(&a->sg_buf[0], 1, wrest, fp);
    }

    fclose(fp);
}
