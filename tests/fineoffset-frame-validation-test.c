#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitbuffer.h"
#include "data.h"
#include "rtl_433_devices.h"

typedef struct {
    int output_calls;
    char model[64];
} capture_t;

static void test_log_output(r_device *decoder, int level, data_t *data)
{
    (void)level;
    (void)decoder;
    data_free(data);
}

static void test_data_output(r_device *decoder, data_t *data)
{
    capture_t *capture = decoder->output_ctx;
    data_t *node       = data;

    if (capture) {
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

static int run_decode(r_device const *template, bitbuffer_t *bits, capture_t *capture)
{
    r_device decoder = *template;

    memset(capture, 0, sizeof(*capture));
    decoder.output_fn  = test_data_output;
    decoder.log_fn     = test_log_output;
    decoder.output_ctx = capture;

    return decoder.decode_fn(&decoder, bits);
}

static int run_decode_code(r_device const *template, char const *code, capture_t *capture)
{
    bitbuffer_t bits = {0};
    bitbuffer_parse(&bits, code);
    return run_decode(template, &bits, capture);
}

static int failf(char const *name, char const *detail)
{
    fprintf(stderr, "FAIL: %s: %s\n", name, detail);
    return 1;
}

static int expect_output(
        char const *name,
        r_device const *template,
        char const *code,
        int expected_outputs,
        char const *expected_model)
{
    capture_t capture;
    int decode_rc = run_decode_code(template, code, &capture);

    if (capture.output_calls != expected_outputs) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                "expected %d output event(s), got %d (decode_rc=%d)",
                expected_outputs, capture.output_calls, decode_rc);
        return failf(name, detail);
    }

    if (expected_outputs > 0) {
        if (!capture.model[0]) {
            return failf(name, "missing model field on emitted event");
        }
        if (strcmp(capture.model, expected_model) != 0) {
            char detail[160];
            snprintf(detail, sizeof(detail),
                    "expected model %s, got %s",
                    expected_model, capture.model);
            return failf(name, detail);
        }
    }

    return 0;
}

static int expect_no_output(char const *name, r_device const *template, char const *code)
{
    return expect_output(name, template, code, 0, NULL);
}

static int expect_no_output_bits(char const *name, r_device const *template, bitbuffer_t *bits)
{
    capture_t capture;
    int decode_rc = run_decode(template, bits, &capture);

    if (capture.output_calls != 0) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                "expected 0 output events, got %d (decode_rc=%d)",
                capture.output_calls, decode_rc);
        return failf(name, detail);
    }

    return 0;
}

static int expect_return_code(char const *name, r_device const *template, char const *code, int expected_rc)
{
    capture_t capture;
    int decode_rc = run_decode_code(template, code, &capture);

    if (decode_rc != expected_rc) {
        char detail[160];
        snprintf(detail, sizeof(detail),
                "expected decode_rc=%d, got %d",
                expected_rc, decode_rc);
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

int main(void)
{
    int failed = 0;
    char code[96];
    bitbuffer_t bits = {0};

    failed += expect_output(
            "valid OOK80 under fineoffset",
            &fineoffset_wh1050,
            "{80}ff5f51934800001246aa",
            1,
            "Fineoffset-WH1050");
    failed += expect_no_output(
            "valid OOK80 under tfa",
            &tfa_303151,
            "{80}ff5f51934800001246aa");

    failed += expect_output(
            "valid OOK79 under fineoffset",
            &fineoffset_wh1050,
            "{79}febea326900000248d54",
            1,
            "Fineoffset-WH1050");
    failed += expect_no_output(
            "valid OOK79 under tfa",
            &tfa_303151,
            "{79}febea326900000248d54");

    failed += expect_output(
            "valid FSK144 under tfa",
            &tfa_303151,
            "{144}00000000aaaaaa2dd45d5193480009000000",
            1,
            "TFA-303151");
    failed += expect_no_output(
            "valid FSK144 under fineoffset",
            &fineoffset_wh1050,
            "{144}00000000aaaaaa2dd45d5193480009000000");

    failed += expect_no_output(
            "truncated FSK120 under tfa",
            &tfa_303151,
            "{120}00000000aaaaaa2dd45d5193480009");

    // The CRC byte and final 24 payload bits are zero, so old boundary handling
    // could synthesize absent trailing bits as zeros and still pass the CRC.
    for (int bits_len = 120; bits_len < 144; ++bits_len) {
        snprintf(code, sizeof(code), "{%d}00000000aaaaaa2dd45d5193480009000000", bits_len);
        failed += expect_no_output("truncated FSK length under tfa", &tfa_303151, code);
    }

    failed += expect_output(
            "exact FSK144 accepted under tfa",
            &tfa_303151,
            "{144}00000000aaaaaa2dd45d5193480009000000",
            1,
            "TFA-303151");

    failed += expect_no_output(
            "invalid CRC OOK under fineoffset",
            &fineoffset_wh1050,
            "{80}ff5f51934800001246ab");
    failed += expect_no_output(
            "invalid CRC FSK under tfa",
            &tfa_303151,
            "{144}00000000aaaaaa2dd45d5193480009000001");

    failed += expect_return_code(
            "invalid 79-bit OOK preamble returns abort length",
            &fineoffset_wh1050,
            "{79}ffbea326900000248d54",
            DECODE_ABORT_LENGTH);
    failed += expect_return_code(
            "invalid 80-bit OOK preamble returns abort length",
            &fineoffset_wh1050,
            "{80}fe5f51934800001246aa",
            DECODE_ABORT_LENGTH);

    failed += expect_rows(
            "two-row OOK fixture row count",
            "{80}ff5f51934800001246aa{80}ff5f51934800001246aa",
            2);
    failed += expect_no_output(
            "two rows under fineoffset",
            &fineoffset_wh1050,
            "{80}ff5f51934800001246aa{80}ff5f51934800001246aa");
    failed += expect_rows(
            "two-row FSK fixture row count",
            "{144}00000000aaaaaa2dd45d5193480009000000{144}00000000aaaaaa2dd45d5193480009000000",
            2);
    failed += expect_no_output(
            "two rows under tfa",
            &tfa_303151,
            "{144}00000000aaaaaa2dd45d5193480009000000{144}00000000aaaaaa2dd45d5193480009000000");

    memset(&bits, 0, sizeof(bits));
    failed += expect_no_output_bits("zero rows under fineoffset", &fineoffset_wh1050, &bits);
    failed += expect_no_output_bits("zero rows under tfa", &tfa_303151, &bits);

    if (failed != 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
