/*
 * A general structure for extracting hierarchical data from the devices;
 * typically key-value pairs, but allows for more rich data as well.
 *
 * Copyright (C) 2015 by Erkki Seppälä <flux@modeemi.fi>
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

#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "limits.h"
// gethostname() needs _XOPEN_SOURCE 500 on unistd.h
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <time.h>

#include "data.h"

typedef void* (*array_elementwise_import_fn)(void*);
typedef void* (*array_element_release_fn)(void*);
typedef void* (*value_release_fn)(void*);

typedef struct {
    /* what is the element size when put inside an array? */
    int array_element_size;

    /* is the element boxed (ie. behind a pointer) when inside an array?
       if it's not boxed ("unboxed"), json dumping function needs to make
       a copy of the value beforehand, because the dumping function only
       deals with boxed values.
     */
    _Bool array_is_boxed;

    /* function for importing arrays. strings are specially handled (as they
       are copied deeply), whereas other arrays are just copied shallowly
       (but copied nevertheles) */
    array_elementwise_import_fn array_elementwise_import;

    /* a function for releasing an element when put in an array; integers
     * don't need to be released, while ie. strings and arrays do. */
    array_element_release_fn array_element_release;

    /* a function for releasing a value. everything needs to be released. */
    value_release_fn value_release;
} data_meta_type_t;

struct data_output;

typedef struct data_output {
    void (*print_data)(struct data_output *output, data_t *data, char *format);
    void (*print_array)(struct data_output *output, data_array_t *data, char *format);
    void (*print_string)(struct data_output *output, const char *data, char *format);
    void (*print_double)(struct data_output *output, double data, char *format);
    void (*print_int)(struct data_output *output, int data, char *format);
    void (*output_free)(struct data_output *output);
    FILE *file;
    void *aux;
} data_output_t;

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
      .value_release            = (value_release_fn) free },

    //  DATA_DOUBLE
    { .array_element_size       = sizeof(double),
      .array_is_boxed           = false,
      .array_elementwise_import = NULL,
      .array_element_release    = NULL,
      .value_release            = (value_release_fn) free },

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

typedef struct {
    const char **fields;
    int          data_recursion;
    const char*  separator;
} data_csv_aux_t;

typedef struct {
    struct sockaddr_in server;
    int sock;
    int pri;
    char hostname[_POSIX_HOST_NAME_MAX + 1];
    char *buf_end;
    size_t buf_size;
} data_syslog_aux_t;

static _Bool import_values(void *dst, void *src, int num_values, data_type_t type)
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
        memcpy(dst, src, element_size * num_values);
    }
    return true; // error is returned early
}

data_array_t *data_array(int num_values, data_type_t type, void *values)
{
    data_array_t *array = calloc(1, sizeof(data_array_t));
    if (array) {
        int element_size = dmt[type].array_element_size;
        array->values = calloc(num_values, element_size);
        if (!array->values)
            goto alloc_error;
        if (!import_values(array->values, values, num_values, type))
            goto alloc_error;

        array->num_values = num_values;
        array->type = type;
    }
    return array;

alloc_error:
    if (array)
        free(array->values);
    free(array);
    return NULL;
}

data_t *data_make(const char *key, const char *pretty_key, ...)
{
    va_list ap;
    data_type_t type;
    va_start(ap, pretty_key);

    data_t *first = NULL;
    data_t *prev = NULL;
    char *format = false;
    type = va_arg(ap, data_type_t);
    do {
        data_t *current;
        void *value = NULL;

        switch (type) {
        case DATA_FORMAT:
            format = strdup(va_arg(ap, char *));
            if (!format)
                goto alloc_error;
            type = va_arg(ap, data_type_t);
            continue;
            break;
        case DATA_COUNT:
            assert(0);
            break;
        case DATA_DATA:
            value = va_arg(ap, data_t *);
            break;
        case DATA_INT:
            value = malloc(sizeof(int));
            if (value)
                *(int *)value = va_arg(ap, int);
            break;
        case DATA_DOUBLE:
            value = malloc(sizeof(double));
            if (value)
                *(double *)value = va_arg(ap, double);
            break;
        case DATA_STRING:
            value = strdup(va_arg(ap, char *));
            break;
        case DATA_ARRAY:
            value = va_arg(ap, data_t *);
            break;
        }

        // also some null arguments are mapped to an alloc error;
        // that's ok, because they originate (typically..) from
        // an alloc error anyway
        if (!value)
            goto alloc_error;

        current = calloc(1, sizeof(*current));
        if (!current)
            goto alloc_error;
        if (prev)
            prev->next = current;

        current->key = strdup(key);
        if (!current->key)
            goto alloc_error;
        current->pretty_key = strdup(pretty_key ? pretty_key : key);
        if (!current->pretty_key)
            goto alloc_error;
        current->type = type;
        current->format = format;
        current->value = value;
        current->next = NULL;

        prev = current;
        if (!first)
            first = current;

        key = va_arg(ap, const char *);
        if (key) {
            pretty_key = va_arg(ap, const char *);
            type = va_arg(ap, data_type_t);
            format = NULL;
        }
    } while (key);
    va_end(ap);

    return first;

alloc_error:
    data_free(first);
    va_end(ap);
    return NULL;
}

void data_array_free(data_array_t *array)
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

void data_free(data_t *data)
{
    while (data) {
        data_t *prev_data = data;
        if (dmt[data->type].value_release)
            dmt[data->type].value_release(data->value);
        free(data->format);
        free(data->pretty_key);
        free(data->key);
        data = data->next;
        free(prev_data);
    }
}

void data_output_print(data_output_t *output, data_t *data)
{
    output->print_data(output, data, NULL);
    if (output->file) {
        fputc('\n', output->file);
        fflush(output->file);
    }
}

void data_output_free(data_output_t *output)
{
    if (!output)
        return;
    output->output_free(output);
}

static void print_value(data_output_t *output, data_type_t type, void *value, char *format)
{
    switch (type) {
    case DATA_FORMAT:
    case DATA_COUNT:
        assert(0);
        break;
    case DATA_DATA:
        output->print_data(output, value, format);
        break;
    case DATA_INT:
        output->print_int(output, *(int *)value, format);
        break;
    case DATA_DOUBLE:
        output->print_double(output, *(double *)value, format);
        break;
    case DATA_STRING:
        output->print_string(output, value, format);
        break;
    case DATA_ARRAY:
        output->print_array(output, value, format);
        break;
    }
}

/* JSON printer */
static void print_json_array(data_output_t *output, data_array_t *array, char *format)
{
    int element_size = dmt[array->type].array_element_size;
    char buffer[element_size];
    fprintf(output->file, "[");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            fprintf(output->file, ", ");
        if (!dmt[array->type].array_is_boxed) {
            memcpy(buffer, (void **)((char *)array->values + element_size * c), element_size);
            print_value(output, array->type, buffer, format);
        } else {
            print_value(output, array->type, *(void **)((char *)array->values + element_size * c), format);
        }
    }
    fprintf(output->file, "]");
}

static void print_json_data(data_output_t *output, data_t *data, char *format)
{
    _Bool separator = false;
    fputc('{', output->file);
    while (data) {
        if (separator)
            fprintf(output->file, ", ");
        output->print_string(output, data->key, NULL);
        fprintf(output->file, " : ");
        print_value(output, data->type, data->value, data->format);
        separator = true;
        data = data->next;
    }
    fputc('}', output->file);
}

static void print_json_string(data_output_t *output, const char *str, char *format)
{
    fprintf(output->file, "\"");
    while (*str) {
        if (*str == '"')
            fputc('\\', output->file);
        fputc(*str, output->file);
        ++str;
    }
    fprintf(output->file, "\"");
}

static void print_json_double(data_output_t *output, double data, char *format)
{
    fprintf(output->file, "%.3f", data);
}

static void print_json_int(data_output_t *output, int data, char *format)
{
    fprintf(output->file, "%d", data);
}

/* Key-Value printer */
static void print_kv_data(data_output_t *output, data_t *data, char *format)
{
    _Bool separator = false;
    _Bool was_labeled = false;
    _Bool written_title = false;
    while (data) {
        _Bool labeled = data->pretty_key[0];
        /* put a : between the first non-labeled and labeled */
        if (separator) {
            if (labeled && !was_labeled && !written_title) {
                fprintf(output->file, "\n");
                written_title = true;
                separator = false;
            } else {
                if (was_labeled)
                    fprintf(output->file, "\n");
                else
                    fprintf(output->file, " ");
            }
        }
        if (!strcmp(data->key, "time"))
            /* fprintf(output->file, "") */;
        else if (!strcmp(data->key, "model"))
            fprintf(output->file, ":\t");
        else
            fprintf(output->file, "\t%s:\t", data->pretty_key);
        if (labeled)
            fputc(' ', output->file);
        print_value(output, data->type, data->value, data->format);
        separator = true;
        was_labeled = labeled;
        data = data->next;
    }
}

static void print_kv_double(data_output_t *output, double data, char *format)
{
    fprintf(output->file, format ? format : "%.3f", data);
}

static void print_kv_int(data_output_t *output, int data, char *format)
{
    fprintf(output->file, format ? format : "%d", data);
}

static void print_kv_string(data_output_t *output, const char *data, char *format)
{
    fprintf(output->file, format ? format : "%s", data);
}

/* CSV printer; doesn't really support recursive data objects yes */
static void print_csv_data(data_output_t *output, data_t *data, char *format)
{
    data_csv_aux_t *csv = output->aux;
    const char **fields = csv->fields;
    int i;

    if (csv->data_recursion)
        return;

    ++csv->data_recursion;
    for (i = 0; fields[i]; ++i) {
        const char *key = fields[i];
        data_t *found = NULL;
        if (i)
            fprintf(output->file, "%s", csv->separator);
        for (data_t *iter = data; !found && iter; iter = iter->next)
            if (strcmp(iter->key, key) == 0)
                found = iter;

        if (found)
            print_value(output, found->type, found->value, found->format);
    }
    --csv->data_recursion;
}

static void print_csv_string(data_output_t *output, const char *str, char *format)
{
    data_csv_aux_t *csv = output->aux;
    while (*str) {
        if (strncmp(str, csv->separator, strlen(csv->separator)) == 0)
            fputc('\\', output->file);
        fputc(*str, output->file);
        ++str;
    }
}

static int compare_strings(const void *a, const void *b)
{
    return strcmp(*(char **)a, *(char **)b);
}

static void *data_csv_init(const char **fields, int num_fields)
{
    data_csv_aux_t *csv = calloc(1, sizeof(data_csv_aux_t));
    int csv_fields = 0;
    int i, j;
    const char **allowed = NULL;
    int *use_count = NULL;
    int num_unique_fields;
    if (!csv)
        goto alloc_error;

    csv->separator = ",";

    allowed = calloc(num_fields, sizeof(const char *));
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

    csv->fields = calloc(num_unique_fields + 1, sizeof(const char **));
    if (!csv->fields)
        goto alloc_error;

    use_count = calloc(num_unique_fields, sizeof(*use_count));
    if (!use_count)
        goto alloc_error;

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
        printf("%s%s", i > 0 ? csv->separator : "", csv->fields[i]);
    }
    printf("\n");
    return csv;

alloc_error:
    free(use_count);
    free(allowed);
    if (csv)
        free(csv->fields);
    free(csv);
    return NULL;
}

static void data_csv_free(data_output_t *output)
{
    data_csv_aux_t *csv = output->aux;
    free(csv->fields);
    free(csv);
    free(output);
}

/* Syslog UDP printer, RFC 5424 (IETF-syslog protocol) */
static void append_buf(data_output_t *output, const char *str)
{
    data_syslog_aux_t *syslog = output->aux;
    size_t len = strlen(str);
    if (syslog->buf_size >= len + 1) {
        strcpy(syslog->buf_end, str);
        syslog->buf_end += len;
        syslog->buf_size -= len;
    }
}

static int snprintf_a(char **restrict str, size_t *size, const char *restrict format, ...)
{
    va_list ap;
    va_start(ap, format);

    int n = vsnprintf(*str, *size, format, ap);
    size_t written = 0;
    if (n > 0) {
        written = (size_t)n < *size ? (size_t)n : *size;
        *size -= written;
        *str += written;
    }
    va_end(ap);
    return n;
}

static void print_syslog_array(data_output_t *output, data_array_t *array, char *format)
{
    int element_size = dmt[array->type].array_element_size;
    char buffer[element_size];
    append_buf(output, "[");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            append_buf(output, ",");
        if (!dmt[array->type].array_is_boxed) {
            memcpy(buffer, (void **)((char *)array->values + element_size * c), element_size);
            print_value(output, array->type, buffer, format);
        } else {
            print_value(output, array->type, *(void **)((char *)array->values + element_size * c), format);
        }
    }
    append_buf(output, "]");
}

static void print_syslog_object(data_output_t *output, data_t *data, char *format)
{
    _Bool separator = false;
    append_buf(output, "{");
    while (data) {
        if (separator)
            append_buf(output, ",");
        output->print_string(output, data->key, NULL);
        append_buf(output, ":");
        print_value(output, data->type, data->value, data->format);
        separator = true;
        data = data->next;
    }
    append_buf(output, "}");
}

static void print_syslog_data(data_output_t *output, data_t *data, char *format)
{
    data_syslog_aux_t *syslog = output->aux;

    if (syslog->buf_end) {
        print_syslog_object(output, data, format);
        return;
    }

    char message[1024];
    syslog->buf_end = message;
    syslog->buf_size = 1024;

    time_t now;
    struct tm tm_info;
    time(&now);
    gmtime_r(&now, &tm_info);
    char timestamp[21];
    strftime(timestamp, 21, "%Y-%m-%dT%H:%M:%SZ", &tm_info);

    snprintf_a(&syslog->buf_end, &syslog->buf_size, "<%d>1 %s %s rtl_433 - - - ", syslog->pri, timestamp, syslog->hostname);

    print_syslog_object(output, data, format);

    int slen = sizeof(syslog->server);

    if (sendto(syslog->sock, message, strlen(message), 0, (struct sockaddr *)&syslog->server, slen) == -1) {
        perror("sendto");
    }

    syslog->buf_end = NULL;
}

static void print_syslog_string(data_output_t *output, const char *str, char *format)
{
    data_syslog_aux_t *syslog = output->aux;
    char *buf = syslog->buf_end;
    size_t size = syslog->buf_size;

    if (size < strlen(str) + 3) {
        return;
    }

    *buf++ = '"';
    size--;
    for (; *str && size >= 3; ++str) {
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

    syslog->buf_end = buf;
    syslog->buf_size = size;
}

static void print_syslog_double(data_output_t *output, double data, char *format)
{
    data_syslog_aux_t *syslog = output->aux;
    snprintf_a(&syslog->buf_end, &syslog->buf_size, "%f", data);
}

static void print_syslog_int(data_output_t *output, int data, char *format)
{
    data_syslog_aux_t *syslog = output->aux;
    snprintf_a(&syslog->buf_end, &syslog->buf_size, "%d", data);
}

static void *data_syslog_init(const char *host, int port)
{
    if (!host || !port)
        return NULL;

    data_syslog_aux_t *syslog = calloc(1, sizeof(data_syslog_aux_t));
    if (!syslog) {
        fprintf(stderr, "calloc() failed");
        return NULL;
    }

    // Severity 5 "Notice", Facility 20 "local use 4"
    syslog->pri = 20 * 8 + 5;

    gethostname(syslog->hostname, _POSIX_HOST_NAME_MAX + 1);
    syslog->hostname[_POSIX_HOST_NAME_MAX] = '\0';

    syslog->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (syslog->sock == -1) {
        perror("socket");
        free(syslog);
        return NULL;
    }

    int broadcast = 1;
    int ret = setsockopt(syslog->sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    memset(&syslog->server, 0, sizeof(syslog->server));
    syslog->server.sin_family = AF_INET;
    syslog->server.sin_port = htons(port);

    struct addrinfo *result = NULL, hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int error = getaddrinfo(host, NULL, &hints, &result);
    if (error) {
        fprintf(stderr, "%s\n", gai_strerror(error));
        close(syslog->sock);
        free(syslog);
        return NULL;
    }
    memcpy(&(syslog->server.sin_addr), &((struct sockaddr_in *)result->ai_addr)->sin_addr, sizeof(struct in_addr));
    freeaddrinfo(result);

    return syslog;
}

static void data_syslog_free(data_output_t *output)
{
    if (!output)
        return;

    data_syslog_aux_t *syslog = output->aux;

    if (syslog->sock != -1) {
        close(syslog->sock);
        syslog->sock = -1;
    }

    free(syslog);
    free(output);
}

// constructors

static void data_json_free(data_output_t *output)
{
    if (!output)
        return;

    free(output);
}

struct data_output *data_output_json_create(FILE *file)
{
    data_output_t *output = calloc(1, sizeof(data_output_t));
    if (!output) {
        fprintf(stderr, "calloc() failed");
        return NULL;
    }

    output->print_data   = print_json_data;
    output->print_array  = print_json_array;
    output->print_string = print_json_string;
    output->print_double = print_json_double;
    output->print_int    = print_json_int;
    output->output_free  = data_json_free;
    output->file         = file;

    return output;
}

struct data_output *data_output_csv_create(FILE *file, const char **fields, int num_fields)
{
    data_output_t *output = calloc(1, sizeof(data_output_t));
    if (!output) {
        fprintf(stderr, "calloc() failed");
        return NULL;
    }

    output->print_data   = print_csv_data;
    output->print_array  = print_json_array;
    output->print_string = print_csv_string;
    output->print_double = print_json_double;
    output->print_int    = print_json_int;
    output->output_free  = data_csv_free;
    output->file         = file;
    output->aux          = data_csv_init(fields, num_fields);

    return output;
}

struct data_output *data_output_kv_create(FILE *file)
{
    data_output_t *output = calloc(1, sizeof(data_output_t));
    if (!output) {
        fprintf(stderr, "calloc() failed");
        return NULL;
    }

    output->print_data   = print_kv_data;
    output->print_array  = print_json_array;
    output->print_string = print_kv_string;
    output->print_double = print_kv_double;
    output->print_int    = print_kv_int;
    output->output_free  = data_json_free;
    output->file         = file;

    return output;
}

struct data_output *data_output_syslog_create(const char *host, int port)
{
    data_output_t *output = calloc(1, sizeof(data_output_t));
    if (!output) {
        fprintf(stderr, "calloc() failed");
        return NULL;
    }

    output->print_data   = print_syslog_data;
    output->print_array  = print_syslog_array;
    output->print_string = print_syslog_string;
    output->print_double = print_syslog_double;
    output->print_int    = print_syslog_int;
    output->output_free  = data_syslog_free;
    output->aux          = data_syslog_init(host, port);

    return output;
}
