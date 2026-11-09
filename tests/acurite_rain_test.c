/** @file
    Regression tests for the Acurite 896 rain gauge decoder.

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
#include "rtl_433_devices.h"

struct capture {
    int count;
    int id;
    double rain_mm;
    char model[32];
};

struct row_spec {
    unsigned bits;
    char const *hex;
    unsigned repeat;
};

struct test_case {
    char const *name;
    struct row_spec const *rows;
    size_t row_specs;
    unsigned expect_rows;
    int expect_ret;
    int expect_count;
    int expect_id;
    double expect_rain;
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

static unsigned build_rows(char *buffer, size_t size, struct row_spec const *rows, size_t row_specs)
{
    size_t used         = 0;
    unsigned total_rows = 0;

    for (size_t i = 0; i < row_specs; ++i) {
        for (unsigned repeat = 0; repeat < rows[i].repeat; ++repeat) {
            append_row(buffer, size, &used, rows[i].bits, rows[i].hex);
            total_rows += 1;
        }
    }

    return total_rows;
}

/* Acceptance vectors in this file are synthetic protocol-shaped rows, not hardware captures. */
static int run_case(struct test_case const *test)
{
    char rows[4096];
    bitbuffer_t bits       = {0};
    struct capture capture = {0};
    r_device decoder       = acurite_rain_896;
    unsigned built_rows;
    int failed = 0;
    int ret;

    built_rows = build_rows(rows, sizeof(rows), test->rows, test->row_specs);
    if (built_rows != test->expect_rows) {
        fprintf(stderr, "FAIL %s: built %u rows, expected fixture size %u\n",
                test->name, built_rows, test->expect_rows);
        return 1;
    }

    decoder.output_fn  = capture_output;
    decoder.log_fn     = free_log_callback;
    decoder.output_ctx = &capture;

    bitbuffer_parse(&bits, rows);
    if (bits.num_rows != test->expect_rows) {
        fprintf(stderr, "FAIL %s: parsed %u rows, expected %u\n",
                test->name, bits.num_rows, test->expect_rows);
        failed = 1;
    }

    ret = decoder.decode_fn(&decoder, &bits);
    if (ret != test->expect_ret) {
        fprintf(stderr, "FAIL %s: return %d, expected %d\n", test->name, ret, test->expect_ret);
        failed = 1;
    }
    if (capture.count != test->expect_count) {
        fprintf(stderr, "FAIL %s: output count %d, expected %d\n",
                test->name, capture.count, test->expect_count);
        failed = 1;
    }

    if (test->expect_count > 0) {
        if (strcmp(capture.model, "Acurite-Rain") != 0) {
            fprintf(stderr, "FAIL %s: model '%s', expected 'Acurite-Rain'\n",
                    test->name, capture.model);
            failed = 1;
        }
        if (capture.id != test->expect_id) {
            fprintf(stderr, "FAIL %s: id %d, expected %d\n",
                    test->name, capture.id, test->expect_id);
            failed = 1;
        }
        if (capture.rain_mm != test->expect_rain) {
            fprintf(stderr, "FAIL %s: rain %.1f, expected %.1f\n",
                    test->name, capture.rain_mm, test->expect_rain);
            failed = 1;
        }
    }

    return failed;
}

static struct row_spec const rows_one_valid_plus_empty[] = {
        {24, "123456", 1},
        {0, "", 11},
};

static struct row_spec const rows_eleven_plus_one_different[] = {
        {24, "123456", 11},
        {24, "234567", 1},
};

static struct row_spec const rows_twelve_distinct[] = {
        {24, "123456", 1},
        {24, "234567", 1},
        {24, "345678", 1},
        {24, "456789", 1},
        {24, "56789a", 1},
        {24, "6789ab", 1},
        {24, "789abc", 1},
        {24, "89abcd", 1},
        {24, "9abcde", 1},
        {24, "abcdef", 1},
        {24, "bcdef1", 1},
        {24, "cdef12", 1},
};

static struct row_spec const rows_twelve_identical_valid[] = {
        {24, "123456", 12},
};

static struct row_spec const rows_sixteen_identical_valid[] = {
        {24, "123456", 16},
};

static struct row_spec const rows_padded_frame_tolerance[] = {
        {32, "12345600", 12},
};

static struct row_spec const rows_eleven_24_one_32[] = {
        {24, "123456", 11},
        {32, "12345600", 1},
};

static struct row_spec const rows_first_row_selection_reject[] = {
        {24, "234567", 1},
        {24, "123456", 12},
};

static struct row_spec const rows_zero_first_byte[] = {
        {24, "003456", 12},
};

static struct row_spec const rows_zero_second_byte[] = {
        {24, "120056", 12},
};

static struct row_spec const rows_zero_third_byte[] = {
        {24, "123400", 12},
};

#if BITBUF_ROWS >= 45
static struct row_spec const rows_issue3693_sparse_45[] = {
        {24, "2fffff", 1},
        {0, "", 17},
        {36, "fffffffff", 1},
        {0, "", 17},
        {36, "fffffffff", 1},
        {0, "", 7},
        {2, "0", 1},
};
#endif

static struct test_case const cases[] = {
        {"one_valid_plus_empty_rows", rows_one_valid_plus_empty, 2, 12, DECODE_ABORT_EARLY, 0, 0, 0.0},
        {"eleven_identical_plus_one_diff", rows_eleven_plus_one_different, 2, 12, DECODE_ABORT_EARLY, 0, 0, 0.0},
        {"twelve_distinct_rows", rows_twelve_distinct, 12, 12, DECODE_ABORT_EARLY, 0, 0, 0.0},
        {"twelve_identical_valid_rows", rows_twelve_identical_valid, 1, 12, 1, 1, 18, 555.0},
        {"sixteen_identical_valid_rows", rows_sixteen_identical_valid, 1, 16, 1, 1, 18, 555.0},
        {"padded_frame_tolerance", rows_padded_frame_tolerance, 1, 12, 1, 1, 18, 555.0},
        {"eleven_24bit_plus_one_32bit", rows_eleven_24_one_32, 2, 12, DECODE_ABORT_EARLY, 0, 0, 0.0},
        {"distinct_row0_then_twelve_valid", rows_first_row_selection_reject, 2, 13, DECODE_ABORT_EARLY, 0, 0, 0.0},
        {"zero_first_byte_reject", rows_zero_first_byte, 1, 12, DECODE_ABORT_EARLY, 0, 0, 0.0},
        {"zero_second_byte_reject", rows_zero_second_byte, 1, 12, DECODE_ABORT_EARLY, 0, 0, 0.0},
        {"zero_third_byte_reject", rows_zero_third_byte, 1, 12, DECODE_ABORT_EARLY, 0, 0, 0.0},
#if BITBUF_ROWS >= 45
        {"issue3693_sparse_45_rows", rows_issue3693_sparse_45, 7, 45, DECODE_ABORT_EARLY, 0, 0, 0.0},
#endif
};

int main(void)
{
    int failed = 0;

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        failed += run_case(&cases[i]);
    }

    if (failed != 0) {
        return 1;
    }

    return 0;
}
