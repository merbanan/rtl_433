/** @file
 * JSON output module.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "fatal.h"

#include "data.h"

static void print_json_array(data_output_t *output, data_array_t *array, char const *format)
{
    link_output_write_char(output->link_output, '[');
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            link_output_printf(output->link_output, ", ");
        print_array_value(output, array, format, c);
    }
    link_output_write_char(output->link_output, ']');
}

static void print_json_data(data_output_t *output, data_t *data, char const *format)
{
    bool separator = false;
    link_output_write_char(output->link_output, '{');
    while (data) {
        if (separator)
            link_output_printf(output->link_output, ", ");
        output->print_string(output, data->key, NULL);
        link_output_printf(output->link_output, " : ");
        print_value(output, data->type, data->value, data->format);
        separator = true;
        data = data->next;
    }
    link_output_write_char(output->link_output, '}');
}

static void print_json_string(data_output_t *output, const char *str, char const *format)
{
    link_output_write_char(output->link_output, '"');
    while (*str) {
        if (*str == '"' || *str == '\\')
            link_output_write_char(output->link_output, '\\');
        link_output_write_char(output->link_output, *str);
        ++str;
    }
    link_output_write_char(output->link_output, '"');
}

static void print_json_double(data_output_t *output, double data, char const *format)
{
    link_output_printf(output->link_output, "%.3f", data);
}

static void print_json_int(data_output_t *output, int data, char const *format)
{
    link_output_printf(output->link_output, "%d", data);
}

static void data_output_json_free(data_output_t *output)
{
    if (!output)
        return;

    link_output_free(output->link_output);

    free(output);
}

struct data_output *data_output_json_create(list_t *links, const char *name, const char *file)
{
    link_t *l;
    data_output_t *output = calloc(1, sizeof(data_output_t));
    if (!output) {
        WARN_CALLOC("data_output_json_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    if (!(l = link_file_create(links, name, file))) {
        free(output);
        return NULL;
    }

    output->print_data   = print_json_data;
    output->print_array  = print_json_array;
    output->print_string = print_json_string;
    output->print_double = print_json_double;
    output->print_int    = print_json_int;
    output->output_free  = data_output_json_free;
    output->link_output  = link_create_output(l);

    return output;
}
