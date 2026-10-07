#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitbuffer.h"
#include "data.h"
#include "decoder_util.h"
#include "rtl_433_devices.h"

struct capture {
    int count;
    int id;
    double rain_mm;
    char model[32];
};

static data_t *find_field(data_t *data, char const *key)
{
    for (; data; data = data->next) {
        if (strcmp(data->key, key) == 0) {
            return data;
        }
    }
    return NULL;
}

static void free_log_callback(r_device *decoder, int level, data_t *data)
{
    (void)decoder;
    (void)level;
    data_free(data);
}

static void capture_output(r_device *decoder, data_t *data)
{
    struct capture *capture = decoder->output_ctx;
    data_t *model_field     = find_field(data, "model");
    data_t *id_field        = find_field(data, "id");
    data_t *rain_field      = find_field(data, "rain_mm");

    capture->count += 1;

    if (model_field && model_field->type == DATA_STRING) {
        snprintf(capture->model, sizeof(capture->model), "%s", (char *)model_field->value.v_ptr);
    }
    if (id_field && id_field->type == DATA_INT) {
        capture->id = id_field->value.v_int;
    }
    if (rain_field && rain_field->type == DATA_DOUBLE) {
        capture->rain_mm = rain_field->value.v_dbl;
    }

    data_free(data);
}

static void append_text(char *buffer, size_t size, size_t *used, char const *text)
{
    int wrote = snprintf(buffer + *used, size - *used, "%s", text);
    if (wrote < 0 || (size_t)wrote >= size - *used) {
        fprintf(stderr, "FAIL: test input buffer overflow while building bitbuffer string\n");
        exit(1);
    }
    *used += (size_t)wrote;
}

static void append_row(char *buffer, size_t size, size_t *used, unsigned bits, char const *hex)
{
    char prefix[32];
    int wrote = snprintf(prefix, sizeof(prefix), "{%u}%s", bits, hex);
    if (wrote < 0 || (size_t)wrote >= sizeof(prefix)) {
        fprintf(stderr, "FAIL: row prefix overflow\n");
        exit(1);
    }
    append_text(buffer, size, used, prefix);
}

static void append_repeat_rows(char *buffer, size_t size, size_t *used, unsigned bits, char const *hex, int count)
{
    for (int i = 0; i < count; ++i) {
        append_row(buffer, size, used, bits, hex);
    }
}

/* Acceptance vectors in this file are synthetic protocol-shaped rows, not hardware captures. */
static int expect_decode(char const *name, char const *rows, int expect_ret, int expect_count, int expect_id, double expect_rain)
{
    bitbuffer_t bits = {0};
    struct capture capture = {0};
    r_device decoder       = acurite_rain_896;
    int failed             = 0;

    decoder.output_fn  = capture_output;
    decoder.log_fn     = free_log_callback;
    decoder.output_ctx = &capture;

    bitbuffer_parse(&bits, rows);

    int ret = decoder.decode_fn(&decoder, &bits);
    if (ret != expect_ret) {
        fprintf(stderr, "FAIL %s: return %d, expected %d\n", name, ret, expect_ret);
        failed = 1;
    }
    if (capture.count != expect_count) {
        fprintf(stderr, "FAIL %s: output count %d, expected %d\n", name, capture.count, expect_count);
        failed = 1;
    }

    if (expect_count > 0) {
        if (strcmp(capture.model, "Acurite-Rain") != 0) {
            fprintf(stderr, "FAIL %s: model '%s', expected 'Acurite-Rain'\n", name, capture.model);
            failed = 1;
        }
        if (capture.id != expect_id) {
            fprintf(stderr, "FAIL %s: id %d, expected %d\n", name, capture.id, expect_id);
            failed = 1;
        }
        if (fabs(capture.rain_mm - expect_rain) > 0.0001) {
            fprintf(stderr, "FAIL %s: rain %.4f, expected %.4f\n", name, capture.rain_mm, expect_rain);
            failed = 1;
        }
    }

    return failed;
}

static int test_one_valid_plus_empty_rows(void)
{
    char rows[256];
    size_t used = 0;

    append_row(rows, sizeof(rows), &used, 24, "123456");
    append_repeat_rows(rows, sizeof(rows), &used, 0, "", 11);

    return expect_decode("one_valid_plus_empty_rows", rows, DECODE_ABORT_EARLY, 0, 0, 0.0);
}

static int test_eleven_identical_plus_one_different(void)
{
    char rows[256];
    size_t used = 0;

    append_repeat_rows(rows, sizeof(rows), &used, 24, "123456", 11);
    append_row(rows, sizeof(rows), &used, 24, "234567");

    return expect_decode("eleven_identical_plus_one_different", rows, DECODE_ABORT_EARLY, 0, 0, 0.0);
}

static int test_twelve_distinct_rows(void)
{
    static char const *const payloads[] = {
            "123456", "234567", "345678", "456789",
            "56789a", "6789ab", "789abc", "89abcd",
            "9abcde", "abcdef", "bcdef1", "cdef12",
    };
    char rows[512];
    size_t used = 0;

    for (size_t i = 0; i < sizeof(payloads) / sizeof(payloads[0]); ++i) {
        append_row(rows, sizeof(rows), &used, 24, payloads[i]);
    }

    return expect_decode("twelve_distinct_rows", rows, DECODE_ABORT_EARLY, 0, 0, 0.0);
}

static int test_twelve_identical_valid_rows(void)
{
    char rows[256];
    size_t used = 0;

    append_repeat_rows(rows, sizeof(rows), &used, 24, "123456", 12);

    return expect_decode("twelve_identical_valid_rows", rows, 1, 1, 18, 555.0);
}

static int test_sixteen_identical_valid_rows(void)
{
    char rows[512];
    size_t used = 0;

    append_repeat_rows(rows, sizeof(rows), &used, 24, "123456", 16);

    return expect_decode("sixteen_identical_valid_rows", rows, 1, 1, 18, 555.0);
}

static int test_padded_frame_tolerance(void)
{
    char rows[256];
    size_t used = 0;

    append_repeat_rows(rows, sizeof(rows), &used, 32, "12345600", 12);

    return expect_decode("padded_frame_tolerance", rows, 1, 1, 18, 555.0);
}

static int test_zero_byte_rejects(void)
{
    int failed = 0;
    char rows[256];
    size_t used = 0;

    used = 0;
    append_repeat_rows(rows, sizeof(rows), &used, 24, "003456", 12);
    failed += expect_decode("zero_first_byte_reject", rows, DECODE_ABORT_EARLY, 0, 0, 0.0);

    used = 0;
    append_repeat_rows(rows, sizeof(rows), &used, 24, "120056", 12);
    failed += expect_decode("zero_second_byte_reject", rows, DECODE_ABORT_EARLY, 0, 0, 0.0);

    used = 0;
    append_repeat_rows(rows, sizeof(rows), &used, 24, "123400", 12);
    failed += expect_decode("zero_third_byte_reject", rows, DECODE_ABORT_EARLY, 0, 0, 0.0);

    return failed;
}

#if BITBUF_ROWS >= 45
static int test_issue3693_sparse_45_rows(void)
{
    char rows[2048];
    size_t used = 0;
    bitbuffer_t bits = {0};
    struct capture capture = {0};
    r_device decoder       = acurite_rain_896;
    int failed             = 0;

    for (int row = 0; row < 45; ++row) {
        if (row == 0) {
            append_row(rows, sizeof(rows), &used, 24, "2fffff");
        }
        else if (row == 18 || row == 36) {
            append_row(rows, sizeof(rows), &used, 36, "fffffffff");
        }
        else if (row == 44) {
            append_row(rows, sizeof(rows), &used, 2, "0");
        }
        else {
            append_row(rows, sizeof(rows), &used, 0, "");
        }
    }

    bitbuffer_parse(&bits, rows);
    if (bits.num_rows != 45) {
        fprintf(stderr, "FAIL issue3693_sparse_45_rows: parsed %u rows, expected 45\n", bits.num_rows);
        return 1;
    }

    decoder.output_fn  = capture_output;
    decoder.log_fn     = free_log_callback;
    decoder.output_ctx = &capture;

    if (decoder.decode_fn(&decoder, &bits) != DECODE_ABORT_EARLY) {
        fprintf(stderr, "FAIL issue3693_sparse_45_rows: expected DECODE_ABORT_EARLY\n");
        failed = 1;
    }
    if (capture.count != 0) {
        fprintf(stderr, "FAIL issue3693_sparse_45_rows: output count %d, expected 0\n", capture.count);
        failed = 1;
    }

    return failed;
}
#endif

int main(void)
{
    int failed = 0;

    failed += test_one_valid_plus_empty_rows();
    failed += test_eleven_identical_plus_one_different();
    failed += test_twelve_distinct_rows();
    failed += test_twelve_identical_valid_rows();
    failed += test_sixteen_identical_valid_rows();
    failed += test_padded_frame_tolerance();
    failed += test_zero_byte_rejects();
#if BITBUF_ROWS >= 45
    failed += test_issue3693_sparse_45_rows();
#endif

    if (failed != 0) {
        return 1;
    }

    return 0;
}
