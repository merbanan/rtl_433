/** @file
 * Pretty Key-Value output module
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

#include "term_ctl.h"
#include "optparse.h"
#include "fatal.h"

#include "data.h"

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
    void *term;
    int color;
    int ring_bell;
    int term_width;
    int data_recursion;
    int column;
} data_output_kv_t;

#define KV_SEP "_ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ _ "

static void print_kv_data(data_output_t *output, data_t *data, char const *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    int color = kv->color;
    int ring_bell = kv->ring_bell;

    // top-level: update width and print separator
    if (!kv->data_recursion) {
        kv->term_width = kv->term ? term_get_columns(kv->term) : 80; // update current term width
        if (color)
            term_set_fg(kv->term, TERM_COLOR_BLACK);
        if (ring_bell)
            term_ring_bell(kv->term);
        char sep[] = KV_SEP KV_SEP KV_SEP KV_SEP;
        if (kv->term_width < (int)sizeof(sep))
            sep[kv->term_width > 0 ? kv->term_width - 1 : 40] = '\0';
        link_output_printf(output->link_output, "%s\n", sep);
        if (color)
            term_set_fg(kv->term, TERM_COLOR_RESET);
    }
    // nested data object: break before
    else {
        if (color)
            term_set_fg(kv->term, TERM_COLOR_RESET);
        link_output_printf(output->link_output, "\n");
        kv->column = 0;
    }

    ++kv->data_recursion;
    while (data) {
        // break before some known keys
        if (kv->column > 0 && kv_break_before_key(data->key)) {
            link_output_printf(output->link_output, "\n");
            kv->column = 0;
        }
        // break if not enough width left
        else if (kv->column >= kv->term_width - 26) {
            link_output_printf(output->link_output, "\n");
            kv->column = 0;
        }
        // pad to next alignment if there is enough width left
        else if (kv->column > 0 && kv->column < kv->term_width - 26) {
            kv->column += link_output_printf(output->link_output, "%*s", 25 - kv->column % 26, " ");
        }

        // print key
        char *key = *data->pretty_key ? data->pretty_key : data->key;
        kv->column += link_output_printf(output->link_output, "%-10s: ", key);
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

        data = data->next;
    }
    --kv->data_recursion;

    // top-level: always end with newline
    if (!kv->data_recursion && kv->column > 0) {
        //link_output_printf(output->link_output, "\n"); // data_output_print() already adds a newline
        kv->column = 0;
    }
}

static void print_kv_array(data_output_t *output, data_array_t *array, char const *format)
{
    //link_output_printf(output->link_output, "[ ");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            link_output_printf(output->link_output, ", ");
        print_array_value(output, array, format, c);
    }
    //link_output_printf(output->link_output, " ]");
}

static void print_kv_double(data_output_t *output, double data, char const *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    kv->column += link_output_printf(output->link_output, format ? format : "%.3f", data);
}

static void print_kv_int(data_output_t *output, int data, char const *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    kv->column += link_output_printf(output->link_output, format ? format : "%d", data);
}

static void print_kv_string(data_output_t *output, const char *data, char const *format)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    kv->column += link_output_printf(output->link_output, format ? format : "%s", data);
}

static void data_output_kv_free(data_output_t *output)
{
    data_output_kv_t *kv = (data_output_kv_t *)output;

    if (!output)
        return;

    if (kv->color)
        term_free(kv->term);

    link_output_free(output->link_output);

    free(output);
}

struct data_output *data_output_kv_create(list_t *links, const char *name, char *param)
{
    char *arg = NULL;
    list_t kwargs = {0};
    link_t *l;
    FILE *f;
    char default_param[2] = "-";

    if (!param || param[0] == '\0')
        param = default_param;

    get_string_and_kwargs(param, &arg, &kwargs);
    if (name) {
        if (!(l = link_search(links, name))) {
            fprintf(stderr, "no such link %s\n", name);
            return NULL;
        }
    } else {
        if (!(l = link_file_create(links, name, arg, &kwargs))) {
            return NULL;
        }
    }

    data_output_kv_t *kv = calloc(1, sizeof(data_output_kv_t));
    if (!kv) {
        WARN_CALLOC("data_output_kv_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    kv->output.print_data   = print_kv_data;
    kv->output.print_array  = print_kv_array;
    kv->output.print_string = print_kv_string;
    kv->output.print_double = print_kv_double;
    kv->output.print_int    = print_kv_int;
    kv->output.output_free  = data_output_kv_free;
    kv->output.link_output  = link_create_output(l, arg, &kwargs);

    if ((f = link_output_get_stream(kv->output.link_output)) != NULL) {
        kv->term = term_init(f);
        kv->color = term_has_color(kv->term);
    }

    kv->ring_bell = 0; // TODO: enable if requested...

    return &kv->output;
}
