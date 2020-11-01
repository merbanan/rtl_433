/** @file
 * CSV output module.
 *
 * Copyright (C) 2015 Christian Zuckschwerdt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "fatal.h"

#include "data.h"

typedef struct {
    struct data_output output;
    const char **fields;
    int data_recursion;
    const char *separator;
} data_output_csv_t;

static void print_csv_data(data_output_t *output, data_t *data, char const *format)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

    const char **fields = csv->fields;
    int i;

    if (csv->data_recursion)
        return;

    int regular = 0; // skip "states" output
    for (data_t *d = data; d; d = d->next) {
        if (!strcmp(d->key, "msg") || !strcmp(d->key, "codes") || !strcmp(d->key, "model")) {
            regular = 1;
            break;
        }
    }
    if (!regular)
        return;

    ++csv->data_recursion;
    for (i = 0; fields[i]; ++i) {
        const char *key = fields[i];
        data_t *found = NULL;
        if (i)
            link_output_printf(output->link_output, "%s", csv->separator);
        for (data_t *iter = data; !found && iter; iter = iter->next)
            if (strcmp(iter->key, key) == 0)
                found = iter;

        if (found)
            print_value(output, found->type, found->value, found->format);
    }
    --csv->data_recursion;
}

static void print_csv_array(data_output_t *output, data_array_t *array, char const *format)
{
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            link_output_write_char(output->link_output, ';');
        print_array_value(output, array, format, c);
    }
}

static void print_csv_string(data_output_t *output, const char *str, char const *format)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

    while (*str) {
        if (strncmp(str, csv->separator, strlen(csv->separator)) == 0)
            link_output_write_char(output->link_output, '\\');
        link_output_write_char(output->link_output, *str);
        ++str;
    }
}

static int compare_strings(const void *a, const void *b)
{
    return strcmp(*(char **)a, *(char **)b);
}

static void data_output_csv_start(struct data_output *output, const char **fields, int num_fields)
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
    memcpy(allowed, fields, sizeof(const char *) * num_fields);

    qsort(allowed, num_fields, sizeof(char *), compare_strings);

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
    free(allowed);
    free(use_count);

    // Output the CSV header
    for (i = 0; csv->fields[i]; ++i) {
        link_output_printf(csv->output.link_output, "%s%s", i > 0 ? csv->separator : "", csv->fields[i]);
    }
    link_output_printf(csv->output.link_output, "\n");
    return;

alloc_error:
    free(use_count);
    free(allowed);
    if (csv) {
        free(csv->fields);
        link_output_free(csv->output.link_output);
    }
    free(csv);
}

static void print_csv_double(data_output_t *output, double data, char const *format)
{
    link_output_printf(output->link_output, "%.3f", data);
}

static void print_csv_int(data_output_t *output, int data, char const *format)
{
    link_output_printf(output->link_output, "%d", data);
}

static void data_output_csv_free(data_output_t *output)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

    link_output_free(csv->output.link_output);
    free(csv->fields);
    free(csv);
}

struct data_output *data_output_csv_create(list_t *links, const char *name, const char *file)
{
    link_t *l;
    data_output_csv_t *csv = calloc(1, sizeof(data_output_csv_t));
    if (!csv) {
        WARN_CALLOC("data_output_csv_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    if (!(l = link_file_create(links, name, file))) {
        free(csv);
        return NULL;
    }

    csv->output.print_data   = print_csv_data;
    csv->output.print_array  = print_csv_array;
    csv->output.print_string = print_csv_string;
    csv->output.print_double = print_csv_double;
    csv->output.print_int    = print_csv_int;
    csv->output.output_start = data_output_csv_start;
    csv->output.output_free  = data_output_csv_free;
    csv->output.link_output  = link_create_output(l);

    return &csv->output;
}
