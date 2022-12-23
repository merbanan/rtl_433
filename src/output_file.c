/** @file
    File outputs for rtl_433 events.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "output_file.h"

#include "data.h"
#include "term_ctl.h"
#include "r_util.h"
#include "logger.h"
#include "fatal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* JSON printer */

typedef struct {
    struct data_output output;
    FILE *file;
} data_output_json_t;

static void R_API_CALLCONV print_json_array(data_output_t *output, data_array_t *array, char const *format)
{
    data_output_json_t *json = (data_output_json_t *)output;

    fprintf(json->file, "[");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            fprintf(json->file, ", ");
        print_array_value(output, array, format, c);
    }
    fprintf(json->file, "]");
}

static void R_API_CALLCONV print_json_data(data_output_t *output, data_t *data, char const *format)
{
    UNUSED(format);
    data_output_json_t *json = (data_output_json_t *)output;

    bool separator = false;
    fputc('{', json->file);
    while (data) {
        if (separator)
            fprintf(json->file, ", ");
        output->print_string(output, data->key, NULL);
        fprintf(json->file, " : ");
        print_value(output, data->type, data->value, data->format);
        separator = true;
        data = data->next;
    }
    fputc('}', json->file);
}

static void R_API_CALLCONV print_json_string(data_output_t *output, const char *str, char const *format)
{
    UNUSED(format);
    data_output_json_t *json = (data_output_json_t *)output;

    size_t str_len = strlen(str);
    if (str[0] == '{' && str[str_len - 1] == '}') {
        // Print embedded JSON object verbatim
        fprintf(json->file, "%s", str);
        return;
    }

    fprintf(json->file, "\"");
    for (; *str; ++str) {
        if (*str == '\r') {
            fprintf(json->file, "\\r");
        }
        else if (*str == '\n') {
            fprintf(json->file, "\\n");
        }
        else if (*str == '\t') {
            fprintf(json->file, "\\t");
        }
        else if (*str == '"' || *str == '\\') {
            fputc('\\', json->file);
            fputc(*str, json->file);
        }
        else {
            fputc(*str, json->file);
        }
    }
    fprintf(json->file, "\"");
}

static void R_API_CALLCONV print_json_double(data_output_t *output, double data, char const *format)
{
    UNUSED(format);
    data_output_json_t *json = (data_output_json_t *)output;

    fprintf(json->file, "%.3f", data);
}

static void R_API_CALLCONV print_json_int(data_output_t *output, int data, char const *format)
{
    UNUSED(format);
    data_output_json_t *json = (data_output_json_t *)output;

    fprintf(json->file, "%d", data);
}

static void R_API_CALLCONV data_output_json_print(data_output_t *output, data_t *data)
{
    data_output_json_t *json = (data_output_json_t *)output;

    if (json && json->file) {
        json->output.print_data(output, data, NULL);
        fputc('\n', json->file);
        fflush(json->file);
    }
}

static void R_API_CALLCONV data_output_json_free(data_output_t *output)
{
    if (!output)
        return;

    free(output);
}

struct data_output *data_output_json_create(int log_level, FILE *file)
{
    data_output_json_t *json = calloc(1, sizeof(data_output_json_t));
    if (!json) {
        WARN_CALLOC("data_output_json_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    json->output.log_level    = log_level;
    json->output.print_data   = print_json_data;
    json->output.print_array  = print_json_array;
    json->output.print_string = print_json_string;
    json->output.print_double = print_json_double;
    json->output.print_int    = print_json_int;
    json->output.output_print = data_output_json_print;
    json->output.output_free  = data_output_json_free;
    json->file                = file;

    return &json->output;
}

/* Pretty Key-Value printer */

static int kv_color_for_key(char const *key)
{
    if (!key || !*key)
        return TERM_COLOR_RESET;
    if (!strcmp(key, "tag") || !strcmp(key, "time"))
        return TERM_COLOR_BLUE;
    if (!strcmp(key, "model") || !strcmp(key, "type") || !strcmp(key, "id"))
        return TERM_COLOR_RED;
    if (!strcmp(key, "mic"))
        return TERM_COLOR_CYAN;
    if (!strcmp(key, "mod") || !strcmp(key, "freq") || !strcmp(key, "freq1") || !strcmp(key, "freq2"))
        return TERM_COLOR_MAGENTA;
    if (!strcmp(key, "rssi") || !strcmp(key, "snr") || !strcmp(key, "noise"))
        return TERM_COLOR_YELLOW;
    return TERM_COLOR_GREEN;
}

static int kv_break_before_key(char const *key)
{
    if (!key || !*key)
        return 0;
    if (!strcmp(key, "model") || !strcmp(key, "mod") || !strcmp(key, "rssi") || !strcmp(key, "codes"))
        return 1;
    return 0;
}

static int kv_break_after_key(char const *key)
{
    if (!key || !*key)
        return 0;
    if (!strcmp(key, "id") || !strcmp(key, "mic"))
        return 1;
    return 0;
}

typedef struct {
    struct data_output output;
    FILE *file;
    void *term;
    int color;
    int ring_bell;
    int term_width;
    int data_recursion;
    int column;
} data_output_kv_t;

#define KV_SEP "_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ "

static void R_API_CALLCONV print_kv_data(data_output_t *output, data_t *data, char const *format)
{
    UNUSED(format);
    data_output_kv_t *kv = (data_output_kv_t *)output;

    int color = kv->color;
    int ring_bell = kv->ring_bell;
    int is_log = 0;

    // top-level: update width and print separator
    if (!kv->data_recursion) {
        // collect well-known top level keys
        data_t *data_src = NULL;
        data_t *data_lvl  = NULL;
        data_t *data_msg  = NULL;
        for (data_t *d = data; d; d = d->next) {
            if (!strcmp(d->key, "src"))
                data_src = d;
            else if (!strcmp(d->key, "lvl"))
                data_lvl = d;
            else if (!strcmp(d->key, "msg"))
                data_msg = d;
        }
        is_log = data_src && data_lvl && data_msg;

        kv->term_width = term_get_columns(kv->term); // update current term width
        if (!is_log) {
        if (color)
            term_set_fg(kv->term, TERM_COLOR_BLACK);
        if (ring_bell)
            term_ring_bell(kv->term);
        char sep[] = KV_SEP KV_SEP KV_SEP KV_SEP;
        if (kv->term_width < (int)sizeof(sep))
            sep[kv->term_width > 0 ? kv->term_width - 1 : 40] = '\0';
        fprintf(kv->file, "%s\n", sep);
        if (color)
            term_set_fg(kv->term, TERM_COLOR_RESET);
        }

        // print special log format
        if (is_log) {
            int level = 0;
            if (data_lvl->type == DATA_INT) {
                level = data_lvl->value.v_int;
            }
            term_color_t src_bg = TERM_COLOR_RESET;
            term_color_t src_fg = TERM_COLOR_RESET;
            if (level == LOG_FATAL) {
                src_bg = TERM_COLOR_BRIGHT_BLACK;
                src_fg = TERM_COLOR_WHITE;
            } else if (level == LOG_CRITICAL) {
                src_bg = TERM_COLOR_BRIGHT_GREEN;
                src_fg = TERM_COLOR_BLACK;
            } else if (level == LOG_ERROR) {
                src_bg = TERM_COLOR_BRIGHT_RED;
                src_fg = TERM_COLOR_WHITE;
            } else if (level == LOG_WARNING) {
                src_bg = TERM_COLOR_BRIGHT_YELLOW;
                src_fg = TERM_COLOR_BLACK;
            } else if (level == LOG_NOTICE) {
                src_bg = TERM_COLOR_BRIGHT_CYAN;
                src_fg = TERM_COLOR_BLACK;
            } else if (level == LOG_INFO) {
                src_bg = TERM_COLOR_BRIGHT_BLUE;
                src_fg = TERM_COLOR_WHITE;
            } else if (level == LOG_DEBUG) {
                src_bg = TERM_COLOR_BRIGHT_MAGENTA;
                src_fg = TERM_COLOR_WHITE;
            } else if (level == LOG_TRACE) {
                src_bg = TERM_COLOR_BRIGHT_BLACK;
                src_fg = TERM_COLOR_WHITE;
            }
            term_set_bg(kv->term, src_bg, src_bg); // hides the brackets
            fprintf(kv->file, "[");
            term_set_bg(kv->term, 0, src_fg);
            print_value(output, data_src->type, data_src->value, data_src->format);
            term_set_bg(kv->term, 0, src_bg); // hides the brackets
            fprintf(kv->file, "]");
            term_set_fg(kv->term, TERM_COLOR_RESET);
            // fprintf(kv->file, " (");
            // print_value(output, data_lvl->type, data_lvl->value, data_lvl->format);
            // fprintf(kv->file, ") ");
            fprintf(kv->file, " ");
            print_value(output, data_msg->type, data_msg->value, data_msg->format);
            // force break on next key
            kv->column = kv->term_width;
        }
    }
    // nested data object: break before
    else {
        if (color)
            term_set_fg(kv->term, TERM_COLOR_RESET);
        fprintf(kv->file, "\n");
        kv->column = 0;
    }

    ++kv->data_recursion;
    for (; data; data = data->next) {
        // skip logging keys
        if (is_log && (!strcmp(data->key, "time") || !strcmp(data->key, "src") || !strcmp(data->key, "lvl")
                || !strcmp(data->key, "msg") || !strcmp(data->key, "num_rows"))) {
            continue;
        }

        // break before some known keys
        if (kv->column > 0 && kv_break_before_key(data->key)) {
            fprintf(kv->file, "\n");
            kv->column = 0;
        }
        // break if not enough width left
        else if (kv->column >= kv->term_width - 26) {
            fprintf(kv->file, "\n");
            kv->column = 0;
        }
        // pad to next alignment if there is enough width left
        else if (kv->column > 0 && kv->column < kv->term_width - 26) {
            kv->column += fprintf(kv->file, "%*s", 25 - kv->column % 26, " ");
        }

        // print key
        char *key = *data->pretty_key ? data->pretty_key : data->key;
        kv->column += fprintf(kv->file, "%-10s: ", key);
        // print value
        if (color)
            term_set_fg(kv->term, kv_color_for_key(data->key));
        print_value(output, data->type, data->value, data->format);
        if (color)
            term_set_fg(kv->term, TERM_COLOR_RESET);

        // force break after some known keys
        if (kv->column > 0 && kv_break_after_key(data->key)) {
            kv->column = kv->term_width; // force break;
        }
    }
    --kv->data_recursion;

    // top-level: always end with newline
    if (!kv->data_recursion && kv->column > 0) {
        //fprintf(kv->file, "\n"); // data_output_print() already adds a newline
        kv->column = 0;
    }
}

static void R_API_CALLCONV print_kv_array(data_output_t *output, data_array_t *array, char const *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    //fprintf(kv->file, "[ ");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            fprintf(kv->file, ", ");
        print_array_value(output, array, format, c);
    }
    //fprintf(kv->file, " ]");
}

static void R_API_CALLCONV print_kv_double(data_output_t *output, double data, char const *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    kv->column += fprintf(kv->file, format ? format : "%.3f", data);
}

static void R_API_CALLCONV print_kv_int(data_output_t *output, int data, char const *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    kv->column += fprintf(kv->file, format ? format : "%d", data);
}

static void R_API_CALLCONV print_kv_string(data_output_t *output, const char *data, char const *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    kv->column += fprintf(kv->file, format ? format : "%s", data);
}

static void R_API_CALLCONV data_output_kv_print(data_output_t *output, data_t *data)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    if (kv && kv->file) {
        kv->output.print_data(output, data, NULL);
        fputc('\n', kv->file);
        fflush(kv->file);
    }
}

static void R_API_CALLCONV data_output_kv_free(data_output_t *output)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    if (!output)
        return;

    if (kv->color)
        term_free(kv->term);

    free(output);
}
struct data_output *data_output_kv_create(int log_level, FILE *file)
{
    data_output_kv_t *kv = calloc(1, sizeof(data_output_kv_t));
    if (!kv) {
        WARN_CALLOC("data_output_kv_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    kv->output.log_level    = log_level;
    kv->output.print_data   = print_kv_data;
    kv->output.print_array  = print_kv_array;
    kv->output.print_string = print_kv_string;
    kv->output.print_double = print_kv_double;
    kv->output.print_int    = print_kv_int;
    kv->output.output_print = data_output_kv_print;
    kv->output.output_free  = data_output_kv_free;
    kv->file                = file;

    kv->term = term_init(file);
    kv->color = term_has_color(kv->term);

    kv->ring_bell = 0; // TODO: enable if requested...

    return &kv->output;
}

/* CSV printer */

typedef struct {
    struct data_output output;
    FILE *file;
    const char **fields;
    const char *separator;
} data_output_csv_t;

static void R_API_CALLCONV print_csv_data(data_output_t *output, data_t *data, char const *format)
{
    UNUSED(format);
    data_output_csv_t *csv = (data_output_csv_t *)output;

    fputc('{', csv->file);
    for (bool separator = false; data; data = data->next) {
        if (separator)
            fprintf(csv->file, "; "); // NOTE: distinct from csv->separator
        output->print_string(output, data->key, NULL);
        fprintf(csv->file, ": ");
        print_value(output, data->type, data->value, data->format);
        separator = true;
    }
    fputc('}', csv->file);
}

static void R_API_CALLCONV print_csv_array(data_output_t *output, data_array_t *array, char const *format)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            fprintf(csv->file, ";");
        print_array_value(output, array, format, c);
    }
}

static void R_API_CALLCONV print_csv_string(data_output_t *output, const char *str, char const *format)
{
    UNUSED(format);
    data_output_csv_t *csv = (data_output_csv_t *)output;

    while (*str) {
        if (strncmp(str, csv->separator, strlen(csv->separator)) == 0)
            fputc('\\', csv->file);
        fputc(*str, csv->file);
        ++str;
    }
}

static int compare_strings(const void *a, const void *b)
{
    return strcmp(*(char **)a, *(char **)b);
}

static void R_API_CALLCONV data_output_csv_start(struct data_output *output, char const *const *fields, int num_fields)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

    int csv_fields = 0;
    int i, j;
    const char **allowed = NULL;
    int *use_count = NULL;
    int num_unique_fields;
    if (!csv)
        goto alloc_error;

    csv->separator = ",";

    allowed = calloc(num_fields, sizeof(const char *));
    if (!allowed) {
        WARN_CALLOC("data_output_csv_start()");
        goto alloc_error;
    }
    memcpy((void *)allowed, fields, sizeof(const char *) * num_fields);

    qsort((void *)allowed, num_fields, sizeof(char *), compare_strings);

    // overwrite duplicates
    i = 0;
    j = 0;
    while (j < num_fields) {
        while (j > 0 && j < num_fields &&
                strcmp(allowed[j - 1], allowed[j]) == 0)
            ++j;

        if (j < num_fields) {
            allowed[i] = allowed[j];
            ++i;
            ++j;
        }
    }
    num_unique_fields = i;

    csv->fields = calloc(num_unique_fields + 1, sizeof(const char *));
    if (!csv->fields) {
        WARN_CALLOC("data_output_csv_start()");
        goto alloc_error;
    }

    use_count = calloc(num_unique_fields + 1, sizeof(*use_count)); // '+ 1' so we never alloc size 0
    if (!use_count) {
        WARN_CALLOC("data_output_csv_start()");
        goto alloc_error;
    }

    for (i = 0; i < num_fields; ++i) {
        const char **field = bsearch(&fields[i], allowed, num_unique_fields, sizeof(const char *),
                compare_strings);
        int *field_use_count = use_count + (field - allowed);
        if (field && !*field_use_count) {
            csv->fields[csv_fields] = fields[i];
            ++csv_fields;
            ++*field_use_count;
        }
    }
    csv->fields[csv_fields] = NULL;
    free((void *)allowed);
    free(use_count);

    // Output the CSV header
    for (i = 0; csv->fields[i]; ++i) {
        fprintf(csv->file, "%s%s", i > 0 ? csv->separator : "", csv->fields[i]);
    }
    fprintf(csv->file, "\n");
    return;

alloc_error:
    free(use_count);
    free((void *)allowed);
    if (csv)
        free((void *)csv->fields);
    free(csv);
}

static void R_API_CALLCONV print_csv_double(data_output_t *output, double data, char const *format)
{
    UNUSED(format);
    data_output_csv_t *csv = (data_output_csv_t *)output;

    fprintf(csv->file, "%.3f", data);
}

static void R_API_CALLCONV print_csv_int(data_output_t *output, int data, char const *format)
{
    UNUSED(format);
    data_output_csv_t *csv = (data_output_csv_t *)output;

    fprintf(csv->file, "%d", data);
}

static void R_API_CALLCONV data_output_csv_print(data_output_t *output, data_t *data)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

    const char **fields = csv->fields;

    int regular = 0; // skip "states" output
    for (data_t *d = data; d; d = d->next) {
        if (!strcmp(d->key, "msg") || !strcmp(d->key, "codes") || !strcmp(d->key, "model")) {
            regular = 1;
            break;
        }
    }
    if (!regular)
        return;

    for (int i = 0; fields[i]; ++i) {
        const char *key = fields[i];
        data_t *found   = NULL;
        if (i)
            fprintf(csv->file, "%s", csv->separator);
        for (data_t *iter = data; !found && iter; iter = iter->next)
            if (strcmp(iter->key, key) == 0)
                found = iter;

        if (found)
            print_value(output, found->type, found->value, found->format);
    }

    fputc('\n', csv->file);
    fflush(csv->file);
}

static void R_API_CALLCONV data_output_csv_free(data_output_t *output)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

    free((void *)csv->fields);
    free(csv);
}

struct data_output *data_output_csv_create(int log_level, FILE *file)
{
    data_output_csv_t *csv = calloc(1, sizeof(data_output_csv_t));
    if (!csv) {
        WARN_CALLOC("data_output_csv_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    csv->output.log_level    = log_level;
    csv->output.print_data   = print_csv_data;
    csv->output.print_array  = print_csv_array;
    csv->output.print_string = print_csv_string;
    csv->output.print_double = print_csv_double;
    csv->output.print_int    = print_csv_int;
    csv->output.output_start = data_output_csv_start;
    csv->output.output_print = data_output_csv_print;
    csv->output.output_free  = data_output_csv_free;
    csv->file                = file;

    return &csv->output;
}
