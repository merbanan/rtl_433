/** @file
    A general structure for extracting hierarchical data from the devices;
    typically key-value pairs, but allows for more rich data as well.

    Copyright (C) 2015 by Erkki Seppälä <flux@modeemi.fi>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "data.h"

#include "abuf.h"
#include "fatal.h"

#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Macro to prevent unused variables (passed into a function)
// from generating a warning.
#define UNUSED(x) (void)(x)

typedef void* (*array_elementwise_import_fn)(void*);
typedef void (*array_element_release_fn)(void*);
typedef void (*value_release_fn)(void*);

typedef struct {
    /* what is the element size when put inside an array? */
    int array_element_size;

    /* is the element boxed (ie. behind a pointer) when inside an array?
       if it's not boxed ("unboxed"), json dumping function needs to make
       a copy of the value beforehand, because the dumping function only
       deals with boxed values.
     */
    bool array_is_boxed;

    /* function for importing arrays. strings are specially handled (as they
       are copied deeply), whereas other arrays are just copied shallowly
       (but copied nevertheless) */
    array_elementwise_import_fn array_elementwise_import;

    /* a function for releasing an element when put in an array; integers
     * don't need to be released, while ie. strings and arrays do. */
    array_element_release_fn array_element_release;

    /* a function for releasing a value. everything needs to be released. */
    value_release_fn value_release;
} data_meta_type_t;

static data_meta_type_t dmt[DATA_COUNT] = {
    //  DATA_DATA
    { .array_element_size       = sizeof(data_t*),
      .array_is_boxed           = true,
      .array_elementwise_import = NULL,
      .array_element_release    = (array_element_release_fn) data_free,
      .value_release            = (value_release_fn) data_free },

    //  DATA_INT
    { .array_element_size       = sizeof(int),
      .array_is_boxed           = false,
      .array_elementwise_import = NULL,
      .array_element_release    = NULL,
      .value_release            = NULL },

    //  DATA_DOUBLE
    { .array_element_size       = sizeof(double),
      .array_is_boxed           = false,
      .array_elementwise_import = NULL,
      .array_element_release    = NULL,
      .value_release            = NULL },

    //  DATA_STRING
    { .array_element_size       = sizeof(char*),
      .array_is_boxed           = true,
      .array_elementwise_import = (array_elementwise_import_fn) strdup,
      .array_element_release    = (array_element_release_fn) free,
      .value_release            = (value_release_fn) free },

    //  DATA_ARRAY
    { .array_element_size       = sizeof(data_array_t*),
      .array_is_boxed           = true,
      .array_elementwise_import = NULL,
      .array_element_release    = (array_element_release_fn) data_array_free ,
      .value_release            = (value_release_fn) data_array_free },
};

static bool import_values(void *dst, void const *src, int num_values, data_type_t type)
{
    int element_size = dmt[type].array_element_size;
    array_elementwise_import_fn import = dmt[type].array_elementwise_import;
    if (import) {
        for (int i = 0; i < num_values; ++i) {
            void *copy = import(*(void **)((char *)src + element_size * i));
            if (!copy) {
                --i;
                while (i >= 0) {
                    free(*(void **)((char *)dst + element_size * i));
                    --i;
                }
                return false;
            } else {
                *((char **)dst + i) = copy;
            }
        }
    } else {
        memcpy(dst, src, (size_t)element_size * num_values);
    }
    return true; // error is returned early
}

/* data */

R_API data_array_t *data_array(int num_values, data_type_t type, void const *values)
{
    if (num_values < 0) {
      return NULL;
    }
    data_array_t *array = calloc(1, sizeof(data_array_t));
    if (!array) {
        WARN_CALLOC("data_array()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    int element_size = dmt[type].array_element_size;
    if (num_values > 0) { // don't alloc empty arrays
        array->values = calloc(num_values, element_size);
        if (!array->values) {
            WARN_CALLOC("data_array()");
            goto alloc_error;
        }
        if (!import_values(array->values, values, num_values, type))
            goto alloc_error;
    }

    array->num_values = num_values;
    array->type       = type;

    return array;

alloc_error:
    if (array)
        free(array->values);
    free(array);
    return NULL;
}

// the static analyzer can't prove the allocs to be correct
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

static data_t *vdata_make(data_t *first, const char *key, const char *pretty_key, va_list ap)
{
    data_type_t type;
    data_t *prev = first;
    while (prev && prev->next)
        prev = prev->next;
    char *format = NULL;
    int skip = 0; // skip the data item if this is set
    type = va_arg(ap, data_type_t);
    do {
        data_t *current;
        data_value_t value = {0};
        // store explicit release function, CSA checker gets confused without this
        value_release_fn value_release = NULL; // appease CSA checker

        switch (type) {
        case DATA_COND:
            skip |= !va_arg(ap, int);
            type = va_arg(ap, data_type_t);
            continue;
        case DATA_FORMAT:
            if (format) {
                fprintf(stderr, "vdata_make() format type used twice\n");
                goto alloc_error;
            }
            format = strdup(va_arg(ap, char *));
            if (!format) {
                WARN_STRDUP("vdata_make()");
                goto alloc_error;
            }
            type = va_arg(ap, data_type_t);
            continue;
        case DATA_COUNT:
            assert(0);
            break;
        case DATA_DATA:
            value_release = (value_release_fn)data_free; // appease CSA checker
            value.v_ptr = va_arg(ap, data_t *);
            break;
        case DATA_INT:
            value.v_int = va_arg(ap, int);
            break;
        case DATA_DOUBLE:
            value.v_dbl = va_arg(ap, double);
            break;
        case DATA_STRING:
            value_release = (value_release_fn)free; // appease CSA checker
            value.v_ptr = strdup(va_arg(ap, char *));
            if (!value.v_ptr)
                WARN_STRDUP("vdata_make()");
            break;
        case DATA_ARRAY:
            value_release = (value_release_fn)data_array_free; // appease CSA checker
            value.v_ptr = va_arg(ap, data_array_t *);
            break;
        default:
            fprintf(stderr, "vdata_make() bad data type (%d)\n", type);
            goto alloc_error;
        }

        if (skip) {
            if (value_release) // could use dmt[type].value_release
                value_release(value.v_ptr);
            free(format);
            format = NULL;
            skip = 0;
        }
        else {
            current = calloc(1, sizeof(*current));
            if (!current) {
                WARN_CALLOC("vdata_make()");
                if (value_release) // could use dmt[type].value_release
                    value_release(value.v_ptr);
                goto alloc_error;
            }
            current->type   = type;
            current->format = format;
            format          = NULL; // consumed
            current->value  = value;
            current->next   = NULL;

            if (prev)
                prev->next = current;
            prev = current;
            if (!first)
                first = current;

            current->key = strdup(key);
            if (!current->key) {
                WARN_STRDUP("vdata_make()");
                goto alloc_error;
            }
            current->pretty_key = strdup(pretty_key ? pretty_key : key);
            if (!current->pretty_key) {
                WARN_STRDUP("vdata_make()");
                goto alloc_error;
            }
        }

        // next args
        key = va_arg(ap, const char *);
        if (key) {
            pretty_key = va_arg(ap, const char *);
            type = va_arg(ap, data_type_t);
        }
    } while (key);
    if (format) {
        fprintf(stderr, "vdata_make() format type without data\n");
        goto alloc_error;
    }

    return first;

alloc_error:
    free(format); // if not consumed
    data_free(first);
    return NULL;
}

R_API data_t *data_make(const char *key, const char *pretty_key, ...)
{
    va_list ap;
    va_start(ap, pretty_key);
    data_t *result = vdata_make(NULL, key, pretty_key, ap);
    va_end(ap);
    return result;
}

R_API data_t *data_append(data_t *first, const char *key, const char *pretty_key, ...)
{
    va_list ap;
    va_start(ap, pretty_key);
    data_t *result = vdata_make(first, key, pretty_key, ap);
    va_end(ap);
    return result;
}

R_API data_t *data_prepend(data_t *first, const char *key, const char *pretty_key, ...)
{
    va_list ap;
    va_start(ap, pretty_key);
    data_t *result = vdata_make(NULL, key, pretty_key, ap);
    va_end(ap);

    if (!result)
        return first;

    data_t *prev = result;
    while (prev->next)
        prev = prev->next;
    prev->next = first;

    return result;
}

R_API void data_array_free(data_array_t *array)
{
    array_element_release_fn release = dmt[array->type].array_element_release;
    if (release) {
        int element_size = dmt[array->type].array_element_size;
        for (int i = 0; i < array->num_values; ++i)
            release(*(void **)((char *)array->values + element_size * i));
    }
    free(array->values);
    free(array);
}

R_API data_t *data_retain(data_t *data)
{
    if (data)
        ++data->retain;
    return data;
}

R_API void data_free(data_t *data)
{
    if (data && data->retain) {
        --data->retain;
        return;
    }
    while (data) {
        data_t *prev_data = data;
        if (dmt[data->type].value_release)
            dmt[data->type].value_release(data->value.v_ptr);
        free(data->format);
        free(data->pretty_key);
        free(data->key);
        data = data->next;
        free(prev_data);
    }
}

#pragma GCC diagnostic pop

/* data output */

R_API void data_output_print(data_output_t *output, data_t *data)
{
    if (!output)
        return;
    if (output->output_print) {
        output->output_print(output, data);
    }
    else {
        output->print_data(output, data, NULL);
    }
}

R_API void data_output_start(struct data_output *output, char const *const *fields, int num_fields)
{
    if (!output || !output->output_start)
        return;
    output->output_start(output, fields, num_fields);
}

R_API void data_output_free(data_output_t *output)
{
    if (!output)
        return;
    output->output_free(output);
}

/* output helpers */

R_API void print_value(data_output_t *output, data_type_t type, data_value_t value, char const *format)
{
    switch (type) {
    case DATA_FORMAT:
    case DATA_COUNT:
    case DATA_COND:
        assert(0);
        break;
    case DATA_DATA:
        output->print_data(output, value.v_ptr, format);
        break;
    case DATA_INT:
        output->print_int(output, value.v_int, format);
        break;
    case DATA_DOUBLE:
        output->print_double(output, value.v_dbl, format);
        break;
    case DATA_STRING:
        output->print_string(output, value.v_ptr, format);
        break;
    case DATA_ARRAY:
        output->print_array(output, value.v_ptr, format);
        break;
    }
}

R_API void print_array_value(data_output_t *output, data_array_t *array, char const *format, int idx)
{
    int element_size = dmt[array->type].array_element_size;
    data_value_t value = {0};

    if (!dmt[array->type].array_is_boxed) {
        memcpy(&value, (char *)array->values + element_size * idx, element_size);
        print_value(output, array->type, value, format);
    } else {
        // Note: on 32-bit data_value_t has different size/alignment than a pointer!
        value.v_ptr = *(void **)((char *)array->values + element_size * idx);
        print_value(output, array->type, value, format);
    }
}

/* JSON string printer */

typedef struct {
    struct data_output output;
    abuf_t msg;
} data_print_jsons_t;

static void R_API_CALLCONV format_jsons_array(data_output_t *output, data_array_t *array, char const *format)
{
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;

    abuf_cat(&jsons->msg, "[");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            abuf_cat(&jsons->msg, ",");
        print_array_value(output, array, format, c);
    }
    abuf_cat(&jsons->msg, "]");
}

static void R_API_CALLCONV format_jsons_object(data_output_t *output, data_t *data, char const *format)
{
    UNUSED(format);
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;

    bool separator = false;
    abuf_cat(&jsons->msg, "{");
    while (data) {
        if (separator)
            abuf_cat(&jsons->msg, ",");
        output->print_string(output, data->key, NULL);
        abuf_cat(&jsons->msg, ":");
        print_value(output, data->type, data->value, data->format);
        separator = true;
        data      = data->next;
    }
    abuf_cat(&jsons->msg, "}");
}

static void R_API_CALLCONV format_jsons_string(data_output_t *output, const char *str, char const *format)
{
    UNUSED(format);
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;

    char *buf   = jsons->msg.tail;
    size_t size = jsons->msg.left;

    size_t str_len = strlen(str);
    if (size < str_len + 3) {
        return;
    }

    if (str[0] == '{' && str[str_len - 1] == '}') {
        // Print embedded JSON object verbatim
        abuf_cat(&jsons->msg, str);
        return;
    }

    *buf++ = '"';
    size--;
    for (; *str && size >= 3; ++str) {
        if (*str == '\r') {
            *buf++ = '\\';
            size--;
            *buf++ = 'r';
            size--;
            continue;
        }
        if (*str == '\n') {
            *buf++ = '\\';
            size--;
            *buf++ = 'n';
            size--;
            continue;
        }
        if (*str == '\t') {
            *buf++ = '\\';
            size--;
            *buf++ = 't';
            size--;
            continue;
        }
        if (*str == '"' || *str == '\\') {
            *buf++ = '\\';
            size--;
        }
        *buf++ = *str;
        size--;
    }
    if (size >= 2) {
        *buf++ = '"';
        size--;
    }
    *buf = '\0';

    jsons->msg.tail = buf;
    jsons->msg.left = size;
}

static void R_API_CALLCONV format_jsons_double(data_output_t *output, double data, char const *format)
{
    UNUSED(format);
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;
    // use scientific notation for very big/small values
    if (data > 1e7 || data < 1e-4) {
        abuf_printf(&jsons->msg, "%g", data);
    }
    else {
        abuf_printf(&jsons->msg, "%.5f", data);
        // remove trailing zeros, always keep one digit after the decimal point
        while (jsons->msg.left > 0 && *(jsons->msg.tail - 1) == '0' && *(jsons->msg.tail - 2) != '.') {
            jsons->msg.tail--;
            jsons->msg.left++;
            *jsons->msg.tail = '\0';
        }
    }
}

static void R_API_CALLCONV format_jsons_int(data_output_t *output, int data, char const *format)
{
    UNUSED(format);
    data_print_jsons_t *jsons = (data_print_jsons_t *)output;
    abuf_printf(&jsons->msg, "%d", data);
}

R_API size_t data_print_jsons(data_t *data, char *dst, size_t len)
{
    data_print_jsons_t jsons = {
            .output = {
                    .print_data   = format_jsons_object,
                    .print_array  = format_jsons_array,
                    .print_string = format_jsons_string,
                    .print_double = format_jsons_double,
                    .print_int    = format_jsons_int,
            },
    };

    abuf_init(&jsons.msg, dst, len);

    format_jsons_object(&jsons.output, data, NULL);

    return len - jsons.msg.left;
}
