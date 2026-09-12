/** @file
    Streaming envelope/FSK detector tests.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "pulse_detect.h"
#include "pulse_detect_fsk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLES 200000
#define PACKAGES 8

static int16_t am[SAMPLES];
static int16_t fm[SAMPLES];

typedef struct {
    unsigned count;
    int type[PACKAGES];
    uint64_t start[PACKAGES];
    uint64_t end[PACKAGES];
    pulse_data_t data[PACKAGES];
} result_t;

typedef struct {
    pulse_detect_t *ook;
    pulse_detect_fsk_t fsk;
    pulse_data_t pulses;
    pulse_data_t fsk_pulses;
    unsigned mode;
} detector_t;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void process(detector_t *d, unsigned offset, unsigned len, result_t *result)
{
    d->fsk_pulses.start_ago += len;
    unsigned calls = 0;
    for (;;) {
        CHECK(++calls < 4 * (len + 1) + 10);
        pulse_detect_span_t span;
        int event = pulse_detect_package(d->ook, am + offset, fm + offset,
                len, 250000, offset, &d->pulses, &span);
        CHECK(span.start <= len);
        CHECK(span.len <= len - span.start);
        int type = 0;
        pulse_data_t *data = &d->pulses;
        if (pulse_detect_fsk_package(&d->fsk, fm + offset, &span, event,
                    &d->pulses, &d->fsk_pulses, d->mode)) {
            pulse_detect_skip_package(d->ook);
            type = PULSE_DATA_FSK;
            data = &d->fsk_pulses;
        }
        else if (event == PULSE_DETECT_OOK) {
            type = PULSE_DATA_OOK;
        }
        if (type) {
            unsigned i = result->count++;
            CHECK(i < PACKAGES);
            CHECK(data->num_pulses > 0 && data->num_pulses <= PD_MAX_PULSES);
            CHECK(offset + len - data->start_ago == d->pulses.offset);
            result->type[i] = type;
            result->start[i] = offset + len - data->start_ago;
            result->end[i] = offset + len - data->end_ago;
            result->data[i] = *data;
            // Compare stream positions, independent of the current buffer end.
            result->data[i].start_ago = 0;
            result->data[i].end_ago = 0;
        }
        if (event == PULSE_DETECT_END)
            break;
    }
}

static void run(detector_t *d, unsigned len, unsigned chunk, result_t *result)
{
    *result = (result_t){0};
    pulse_detect_reset(d->ook);
    pulse_detect_fsk_init(&d->fsk);
    pulse_data_clear(&d->pulses);
    pulse_data_clear(&d->fsk_pulses);
    for (unsigned offset = 0; offset < len;) {
        unsigned n = len - offset < chunk ? len - offset : chunk;
        process(d, offset, n, result);
        offset += n;
    }
    process(d, len, 0, result);
    unsigned count = result->count;
    process(d, len, 0, result);
    CHECK(result->count == count); // Repeated flush cannot duplicate a package.
}

static void carrier(unsigned start, unsigned len, int fsk)
{
    CHECK(start + len <= SAMPLES);
    for (unsigned i = start; i < start + len; ++i) {
        am[i] = 8000;
        fm[i] = fsk ? (((i - start) / 64) % 2 ? -6000 : 6000) : 3500;
    }
}

static void check_chunks(detector_t *d, char const *name, unsigned len,
        unsigned count, int first_type)
{
    // Include single samples and boundaries around the short-gap filter.
    unsigned const chunks[] = {1, 9, 10, 11, 63, 64, 127, 1024, 2048, 4096, 6154};
    result_t reference;
    result_t actual;
    run(d, len, SAMPLES, &reference);
    CHECK(reference.count == count);
    if (count) {
        CHECK(reference.type[0] == first_type);
        CHECK(reference.start[0] == 2048);
        if (first_type == PULSE_DATA_FSK) {
            CHECK(reference.data[0].num_pulses > PD_MIN_PULSES);
            CHECK(abs(reference.data[0].fsk_f1_est - reference.data[0].fsk_f2_est) > 3000);
        }
        else {
            CHECK(reference.data[0].fsk_f1_est > 0);
        }
    }
    for (unsigned i = 0; i < sizeof(chunks) / sizeof(*chunks); ++i) {
        run(d, len, chunks[i], &actual);
        if (actual.count != reference.count) {
            fprintf(stderr, "%s mode %u chunk %u: %u packages, expected %u\n",
                    name, d->mode, chunks[i], actual.count, reference.count);
            exit(1);
        }
        for (unsigned j = 0; j < count; ++j) {
            pulse_data_t const *a = &actual.data[j];
            pulse_data_t const *b = &reference.data[j];
            if (actual.type[j] != reference.type[j] || actual.start[j] != reference.start[j]
                    || actual.end[j] != reference.end[j]
                    || a->offset != b->offset || a->num_pulses != b->num_pulses
                    || a->fsk_f1_est != b->fsk_f1_est || a->fsk_f2_est != b->fsk_f2_est
                    || a->ook_low_estimate != b->ook_low_estimate
                    || a->ook_high_estimate != b->ook_high_estimate
                    || memcmp(a->pulse, b->pulse, a->num_pulses * sizeof(*a->pulse))
                    || memcmp(a->gap, b->gap, a->num_pulses * sizeof(*a->gap))) {
                fprintf(stderr, "%s mode %u chunk %u: package %u differs\n",
                        name, d->mode, chunks[i], j);
                exit(1);
            }
        }
    }
}

int main(void)
{
    detector_t d = {.ook = pulse_detect_create()};
    CHECK(d.ook);
    for (d.mode = FSK_PULSE_DETECT_OLD; d.mode <= FSK_PULSE_DETECT_NEW; ++d.mode) {
        memset(am, 0, sizeof(am));
        memset(fm, 0, sizeof(fm));
        check_chunks(&d, "empty", 0, 0, 0);
        check_chunks(&d, "silence", SAMPLES, 0, 0);

        carrier(2048, 800, 0);
        check_chunks(&d, "constant-frequency OOK", SAMPLES, 1, PULSE_DATA_OOK);
        check_chunks(&d, "OOK flush in pulse", 2500, 1, PULSE_DATA_OOK);
        check_chunks(&d, "OOK flush in short gap", 2850, 1, PULSE_DATA_OOK);
        memset(am, 0, sizeof(am));
        carrier(2048, 5, 0);
        check_chunks(&d, "spurious first pulse", SAMPLES, 0, 0);

        memset(am, 0, sizeof(am));
        carrier(2048, 4096, 1);
        check_chunks(&d, "FSK", SAMPLES, 1, PULSE_DATA_FSK);
        check_chunks(&d, "FSK flush in pulse", 6000, 1, PULSE_DATA_FSK);
        check_chunks(&d, "FSK flush in short gap", 6145, 1, PULSE_DATA_FSK);
        for (unsigned gap = 1; gap <= 10; ++gap) {
            carrier(2048, 4096, 1);
            memset(am + 4096, 0, gap * sizeof(*am));
            check_chunks(&d, "FSK short gap", SAMPLES, 1, PULSE_DATA_FSK);
        }

        carrier(2048, 4096, 1);
        carrier(6156, 4096, 1);
        check_chunks(&d, "adjacent FSK packages", SAMPLES, 2, PULSE_DATA_FSK);
        carrier(14000, 100, 0);
        check_chunks(&d, "FSK followed by OOK", SAMPLES, 3, PULSE_DATA_FSK);

        memset(am, 0, sizeof(am));
        carrier(2048, 175000, 1);
        check_chunks(&d, "FSK pulse buffer rollover", SAMPLES, 1, PULSE_DATA_FSK);

        memset(am, 0, sizeof(am));
        for (unsigned i = 0; i <= PD_MAX_PULSES; ++i)
            carrier(2048 + i * 128, 64, 0);
        check_chunks(&d, "OOK pulse buffer full", SAMPLES, 2, PULSE_DATA_OOK);

        memset(am, 0, sizeof(am));
        carrier(2048, 4096, 1);
        result_t partial = {0};
        pulse_detect_reset(d.ook);
        pulse_detect_fsk_init(&d.fsk);
        process(&d, 0, 5000, &partial);
        CHECK(partial.count == 0);
        pulse_detect_reset(d.ook);
        pulse_detect_fsk_init(&d.fsk);
        process(&d, 0, 0, &partial);
        CHECK(partial.count == 0); // Retuning discards a partially received carrier.
    }
    pulse_detect_free(d.ook);
    puts("Pulse detector streaming tests passed");
    return 0;
}
