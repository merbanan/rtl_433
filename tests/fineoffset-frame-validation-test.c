/** @file
    Regression tests for Fine Offset WH1050 frame validation and accounting.

    Copyright (C) 2026 Vryuz

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitbuffer.h"
#include "data.h"
#include "pulse_slicer.h"
#include "rtl_433_devices.h"

typedef struct {
    int output_calls;
    char model[64];
} capture_t;

typedef struct {
    char const *name;
    r_device const *device_template;
    char const *code;
    int expected_rc;
    int expected_outputs;
    char const *expected_model;
} decode_case_t;

static void test_log_output(r_device *decoder, int level, data_t *data)
{
    (void)decoder;
    (void)level;
    data_free(data);
}

static void test_data_output(r_device *decoder, data_t *data)
{
    capture_t *capture = decoder->output_ctx;

    if (capture) {
        data_t *node = data;

        capture->output_calls++;
        for (; node; node = node->next) {
            if (node->key && !strcmp(node->key, "model") && node->type == DATA_STRING && node->value.v_ptr) {
                snprintf(capture->model, sizeof(capture->model), "%s", (char *)node->value.v_ptr);
                break;
            }
        }
    }

    data_free(data);
}

static void init_test_device(r_device *decoder, r_device const *device_template, capture_t *capture)
{
    *decoder = *device_template;
    memset(capture, 0, sizeof(*capture));
    decoder->output_fn  = test_data_output;
    decoder->log_fn     = test_log_output;
    decoder->output_ctx = capture;
}

static int failf(char const *name, char const *detail)
{
    fprintf(stderr, "FAIL: %s: %s\n", name, detail);
    return 1;
}

static int run_decode_case(decode_case_t const *test_case)
{
    bitbuffer_t bits = {0};
    capture_t capture;
    r_device decoder;
    int decode_rc;

    bitbuffer_parse(&bits, test_case->code);
    init_test_device(&decoder, test_case->device_template, &capture);
    decode_rc = decoder.decode_fn(&decoder, &bits);

    if (decode_rc != test_case->expected_rc) {
        char detail[192];
        snprintf(detail, sizeof(detail),
                "expected decode_rc=%d, got %d",
                test_case->expected_rc, decode_rc);
        return failf(test_case->name, detail);
    }

    if (capture.output_calls != test_case->expected_outputs) {
        char detail[192];
        snprintf(detail, sizeof(detail),
                "expected %d output event(s), got %d",
                test_case->expected_outputs, capture.output_calls);
        return failf(test_case->name, detail);
    }

    if (test_case->expected_outputs > 0) {
        if (!capture.model[0]) {
            return failf(test_case->name, "missing model field on emitted event");
        }
        if (strcmp(capture.model, test_case->expected_model) != 0) {
            char detail[192];
            snprintf(detail, sizeof(detail),
                    "expected model %s, got %s",
                    test_case->expected_model, capture.model);
            return failf(test_case->name, detail);
        }
    }

    return 0;
}

static int expect_no_output_bits(char const *name, r_device const *device_template, bitbuffer_t *bits)
{
    capture_t capture;
    r_device decoder;
    int decode_rc;

    init_test_device(&decoder, device_template, &capture);
    decode_rc = decoder.decode_fn(&decoder, bits);

    if (decode_rc != DECODE_ABORT_EARLY) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                "expected decode_rc=%d, got %d",
                DECODE_ABORT_EARLY,
                decode_rc);
        return failf(name, detail);
    }
    if (capture.output_calls != 0) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                "expected 0 output events, got %d",
                capture.output_calls);
        return failf(name, detail);
    }

    return 0;
}

static int expect_rows(char const *name, char const *code, unsigned expected_rows)
{
    bitbuffer_t bits = {0};

    bitbuffer_parse(&bits, code);
    if (bits.num_rows != expected_rows) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                "expected %u rows, got %u",
                expected_rows, bits.num_rows);
        return failf(name, detail);
    }

    return 0;
}

static int expect_truncated_fsk_no_output(int bit_len)
{
    bitbuffer_t bits = {0};
    capture_t capture;
    r_device decoder;
    char code[96];
    char name[96];
    int decode_rc;

    snprintf(code, sizeof(code), "{%d}00000000aaaaaa2dd45d5193480009000000", bit_len);
    snprintf(name, sizeof(name), "truncated FSK len %d under tfa", bit_len);

    bitbuffer_parse(&bits, code);
    init_test_device(&decoder, &tfa_303151, &capture);
    decode_rc = decoder.decode_fn(&decoder, &bits);

    if (decode_rc != 0 || capture.output_calls != 0) {
        char detail[192];
        snprintf(detail, sizeof(detail),
                "bit length %d expected decode_rc=0/output=0, got decode_rc=%d/output=%d",
                bit_len, decode_rc, capture.output_calls);
        return failf(name, detail);
    }

    return 0;
}

static int expect_slicer_accounting(char const *name, char const *code)
{
    capture_t capture;
    r_device decoder;
    int ret;

    init_test_device(&decoder, &fineoffset_wh1050, &capture);
    ret = pulse_slicer_string(code, &decoder);

    if (ret != 1) {
        char detail[160];
        snprintf(detail, sizeof(detail), "expected pulse_slicer_string() return 1, got %d", ret);
        return failf(name, detail);
    }
    if (capture.output_calls != 1) {
        char detail[160];
        snprintf(detail, sizeof(detail), "expected 1 output event, got %d", capture.output_calls);
        return failf(name, detail);
    }
    if (decoder.decode_events != 1 || decoder.decode_ok != 1 || decoder.decode_messages != 1) {
        char detail[192];
        snprintf(detail, sizeof(detail),
                "expected events/ok/messages = 1/1/1, got %u/%u/%u",
                decoder.decode_events, decoder.decode_ok, decoder.decode_messages);
        return failf(name, detail);
    }
    if (decoder.decode_fails[0] != 0) {
        char detail[160];
        snprintf(detail, sizeof(detail), "expected decode_fails[0]=0, got %u", decoder.decode_fails[0]);
        return failf(name, detail);
    }

    return 0;
}

int main(void)
{
    bitbuffer_t bits = {0};
    int failed       = 0;
    int bit_len;

    static decode_case_t const decode_cases[] = {
            {"valid OOK80 under fineoffset", &fineoffset_wh1050, "{80}ff5f51934800001246aa", 1, 1, "Fineoffset-WH1050"},
            {"valid OOK80 under tfa", &tfa_303151, "{80}ff5f51934800001246aa", DECODE_ABORT_LENGTH, 0, NULL},
            {"valid OOK79 under fineoffset", &fineoffset_wh1050, "{79}febea326900000248d54", 1, 1, "Fineoffset-WH1050"},
            {"valid OOK79 under tfa", &tfa_303151, "{79}febea326900000248d54", DECODE_ABORT_LENGTH, 0, NULL},
            {"valid FSK144 under tfa", &tfa_303151, "{144}00000000aaaaaa2dd45d5193480009000000", 1, 1, "TFA-303151"},
            {"valid FSK144 under fineoffset", &fineoffset_wh1050, "{144}00000000aaaaaa2dd45d5193480009000000", DECODE_ABORT_LENGTH, 0, NULL},
            {"truncated FSK120 under tfa", &tfa_303151, "{120}00000000aaaaaa2dd45d5193480009", 0, 0, NULL},
            {"invalid CRC OOK under fineoffset", &fineoffset_wh1050, "{80}ff5f51934800001246ab", 0, 0, NULL},
            {"invalid CRC FSK under tfa", &tfa_303151, "{144}00000000aaaaaa2dd45d5193480009000001", 0, 0, NULL},
            {"invalid 79-bit OOK preamble returns abort length", &fineoffset_wh1050, "{79}ffbea326900000248d54", DECODE_ABORT_LENGTH, 0, NULL},
            {"invalid 80-bit OOK preamble returns abort length", &fineoffset_wh1050, "{80}fe5f51934800001246aa", DECODE_ABORT_LENGTH, 0, NULL},
            {"two rows under fineoffset", &fineoffset_wh1050, "{80}ff5f51934800001246aa{80}ff5f51934800001246aa", DECODE_ABORT_EARLY, 0, NULL},
            {"two rows under tfa", &tfa_303151, "{144}00000000aaaaaa2dd45d5193480009000000{144}00000000aaaaaa2dd45d5193480009000000", DECODE_ABORT_EARLY, 0, NULL},
    };

    for (unsigned i = 0; i < sizeof(decode_cases) / sizeof(decode_cases[0]); ++i) {
        failed += run_decode_case(&decode_cases[i]);
    }

    // The CRC byte and final 24 payload bits are zero, so old boundary handling
    // could synthesize absent trailing bits as zeros and still pass the CRC.
    for (bit_len = 120; bit_len < 144; ++bit_len) {
        failed += expect_truncated_fsk_no_output(bit_len);
    }

    failed += expect_slicer_accounting("pulse slicer accounting for valid OOK80", "{80}ff5f51934800001246aa");
    failed += expect_slicer_accounting("pulse slicer accounting for valid OOK79", "{79}febea326900000248d54");

    failed += expect_rows(
            "two-row OOK fixture row count",
            "{80}ff5f51934800001246aa{80}ff5f51934800001246aa",
            2);
    failed += expect_rows(
            "two-row FSK fixture row count",
            "{144}00000000aaaaaa2dd45d5193480009000000{144}00000000aaaaaa2dd45d5193480009000000",
            2);

    memset(&bits, 0, sizeof(bits));
    failed += expect_no_output_bits("zero rows under fineoffset", &fineoffset_wh1050, &bits);
    failed += expect_no_output_bits("zero rows under tfa", &tfa_303151, &bits);

    if (failed != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
