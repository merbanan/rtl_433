/** @file
    Log outputs for rtl_433 events.

    Copyright (C) 2022 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "output_log.h"

#include "data.h"
#include "r_util.h"
#include "fatal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* LOG printer */

typedef struct {
    struct data_output output;
    FILE *file;
} data_output_log_t;

static void R_API_CALLCONV print_log_array(data_output_t *output, data_array_t *array, char const *format)
{
    data_output_log_t *log = (data_output_log_t *)output;

    fprintf(log->file, "[");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            fprintf(log->file, ", ");
        print_array_value(output, array, format, c);
    }
    fprintf(log->file, "]");
}

static void R_API_CALLCONV print_log_data(data_output_t *output, data_t *data, char const *format)
{
    UNUSED(format);
    data_output_log_t *log = (data_output_log_t *)output;

    fputc('{', log->file);
    for (bool separator = false; data; data = data->next) {
        if (separator)
            fprintf(log->file, ", ");
        output->print_string(output, data->key, NULL);
        fprintf(log->file, ": ");
        print_value(output, data->type, data->value, data->format);
        separator = true;
    }
    fputc('}', log->file);
}

static void R_API_CALLCONV print_log_string(data_output_t *output, const char *str, char const *format)
{
    UNUSED(format);
    data_output_log_t *log = (data_output_log_t *)output;

    fprintf(log->file, "%s", str);
}

static void R_API_CALLCONV print_log_double(data_output_t *output, double data, char const *format)
{
    UNUSED(format);
    data_output_log_t *log = (data_output_log_t *)output;

    fprintf(log->file, "%.3f", data);
}

static void R_API_CALLCONV print_log_int(data_output_t *output, int data, char const *format)
{
    UNUSED(format);
    data_output_log_t *log = (data_output_log_t *)output;

    fprintf(log->file, "%d", data);
}

static void R_API_CALLCONV data_output_log_print(data_output_t *output, data_t *data)
{
    data_output_log_t *log = (data_output_log_t *)output;

    // collect well-known top level keys
    data_t *data_src = NULL;
    data_t *data_lvl = NULL;
    data_t *data_msg = NULL;
    for (data_t *d = data; d; d = d->next) {
        if (!strcmp(d->key, "src"))
            data_src = d;
        else if (!strcmp(d->key, "lvl"))
            data_lvl = d;
        else if (!strcmp(d->key, "msg"))
            data_msg = d;
    }

    int is_log = data_src && data_lvl && data_msg;
    if (!is_log) {
        return; // print log messages only
    }

    // int level = 0;
    // if (data_lvl->type == DATA_INT) {
    //     level = data_lvl->value.v_int;
    // }
    print_value(output, data_src->type, data_src->value, data_src->format);
    // fprintf(log->file, "(");
    // print_value(output, data_lvl->type, data_lvl->value, data_lvl->format);
    // fprintf(log->file, ") ");
    fprintf(log->file, ": ");
    print_value(output, data_msg->type, data_msg->value, data_msg->format);

    for (; data; data = data->next) {
        // skip logging keys
        if (!strcmp(data->key, "time")
                || !strcmp(data->key, "src")
                || !strcmp(data->key, "lvl")
                || !strcmp(data->key, "msg")
                || !strcmp(data->key, "num_rows")) {
            continue;
        }

        fprintf(log->file, " ");
        output->print_string(output, data->key, NULL);
        fprintf(log->file, " ");
        print_value(output, data->type, data->value, data->format);
    }

    fputc('\n', log->file);
    fflush(log->file);
}

static void R_API_CALLCONV data_output_log_free(data_output_t *output)
{
    if (!output) {
        return;
    }
    free(output);
}

struct data_output *data_output_log_create(int log_level, FILE *file)
{
    data_output_log_t *log = calloc(1, sizeof(data_output_log_t));
    if (!log) {
        WARN_CALLOC("data_output_log_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    if (!file) {
        file = stderr; // print to stderr by default
    }

    log->output.log_level    = log_level;
    log->output.print_data   = print_log_data;
    log->output.print_array  = print_log_array;
    log->output.print_string = print_log_string;
    log->output.print_double = print_log_double;
    log->output.print_int    = print_log_int;
    log->output.output_print = data_output_log_print;
    log->output.output_free  = data_output_log_free;
    log->file                = file;

    return (struct data_output *)log;
}
