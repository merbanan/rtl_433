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

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
  #if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
  #undef _WIN32_WINNT
  #define _WIN32_WINNT 0x0600   /* Needed to pull in 'struct sockaddr_storage' */
  #endif

  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <netdb.h>
  #include <netinet/in.h>

  #define SOCKET          int
  #define INVALID_SOCKET  -1
#endif

#include <time.h>

#include "data.h"

#ifdef _WIN32
  #define _POSIX_HOST_NAME_MAX  128
  #undef  close   /* We only work with sockets here */
  #define close(s)              closesocket (s)
  #define perror(str)           ws2_perror (str)

  static void ws2_perror (const char *str)
  {
    if (str && *str)
       fprintf (stderr, "%s: ", str);
    fprintf (stderr, "Winsock error %d.\n", WSAGetLastError());
  }
#endif

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

struct data_output;

typedef struct data_output {
    void (*print_data)(struct data_output *output, data_t *data, char *format);
    void (*print_array)(struct data_output *output, data_array_t *data, char *format);
    void (*print_string)(struct data_output *output, const char *data, char *format);
    void (*print_double)(struct data_output *output, double data, char *format);
    void (*print_int)(struct data_output *output, int data, char *format);
    void (*output_free)(struct data_output *output);
    FILE *file;
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

static bool import_values(void *dst, void *src, int num_values, data_type_t type)
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

static data_t *vdata_make(data_t *first, const char *key, const char *pretty_key, va_list ap)
{
    data_type_t type;
    data_t *prev = first;
    while (prev && prev->next)
        prev = prev->next;
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
    return NULL;
}

data_t *data_make(const char *key, const char *pretty_key, ...)
{
    va_list ap;
    va_start(ap, pretty_key);
    data_t *result = vdata_make(NULL, key, pretty_key, ap);
    va_end(ap);
    return result;
}

data_t *data_append(data_t *first, const char *key, const char *pretty_key, ...)
{
    va_list ap;
    va_start(ap, pretty_key);
    data_t *result = vdata_make(first, key, pretty_key, ap);
    va_end(ap);
    return result;
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

static void print_array_value(data_output_t *output, data_array_t *array, char *format, int idx)
{
    int element_size = dmt[array->type].array_element_size;
#ifdef RTL_433_NO_VLAs
    char *buffer = alloca (element_size);
#else
    char buffer[element_size];
#endif
    
    if (!dmt[array->type].array_is_boxed) {
        memcpy(buffer, (void **)((char *)array->values + element_size * idx), element_size);
        print_value(output, array->type, buffer, format);
    } else {
        print_value(output, array->type, *(void **)((char *)array->values + element_size * idx), format);
    }
}

/* JSON printer */

static void print_json_array(data_output_t *output, data_array_t *array, char *format)
{
    fprintf(output->file, "[");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            fprintf(output->file, ", ");
        print_array_value(output, array, format, c);
    }
    fprintf(output->file, "]");
}

static void print_json_data(data_output_t *output, data_t *data, char *format)
{
    bool separator = false;
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

static void data_output_json_free(data_output_t *output)
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
    output->output_free  = data_output_json_free;
    output->file         = file;

    return output;
}

/* Key-Value printer */

static void print_kv_data(data_output_t *output, data_t *data, char *format)
{
    bool separator = false;
    bool was_labeled = false;
    bool written_title = false;
    while (data) {
        bool labeled = data->pretty_key[0];
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
    output->output_free  = data_output_json_free;
    output->file         = file;

    return output;
}

/* CSV printer; doesn't really support recursive data objects yet */

typedef struct {
    struct data_output output;
    const char **fields;
    int data_recursion;
    const char *separator;
} data_output_csv_t;

static void print_csv_data(data_output_t *output, data_t *data, char *format)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

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

static void print_csv_array(data_output_t *output, data_array_t *array, char *format)
{
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            fprintf(output->file, ";");
        print_array_value(output, array, format, c);
    }
}

static void print_csv_string(data_output_t *output, const char *str, char *format)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

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

static void *data_output_csv_init(data_output_csv_t *csv, const char **fields, int num_fields)
{
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

static void data_output_csv_free(data_output_t *output)
{
    data_output_csv_t *csv = (data_output_csv_t *)output;

    free(csv->fields);
    free(csv);
}

struct data_output *data_output_csv_create(FILE *file, const char **fields, int num_fields)
{
    data_output_csv_t *csv = calloc(1, sizeof(data_output_csv_t));
    if (!csv) {
        fprintf(stderr, "calloc() failed");
        return NULL;
    }

    csv->output.print_data   = print_csv_data;
    csv->output.print_array  = print_csv_array;
    csv->output.print_string = print_csv_string;
    csv->output.print_double = print_json_double;
    csv->output.print_int    = print_json_int;
    csv->output.output_free  = data_output_csv_free;
    csv->output.file         = file;
    data_output_csv_init(csv, fields, num_fields);

    return &csv->output;
}

/* Datagram (UDP) client */

typedef struct {
    struct sockaddr_storage addr;
    socklen_t addr_len;
    SOCKET sock;
} datagram_client_t;

static int datagram_client_open(datagram_client_t *client, const char *host, const char *port)
{
    if (!host || !port)
        return -1;

    struct addrinfo hints, *res, *res0;
    int    error;
    SOCKET sock;
    const char *cause = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG;
    error = getaddrinfo(host, port, &hints, &res0);
    if (error) {
        fprintf(stderr, "%s\n", gai_strerror(error));
        return -1;
    }
    sock = INVALID_SOCKET;
    for (res = res0; res; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock >= 0) {
            client->sock = sock;
            memset(&client->addr, 0, sizeof(client->addr));
            memcpy(&client->addr, res->ai_addr, res->ai_addrlen);
            client->addr_len = res->ai_addrlen;
            break; // success
        }
    }
    freeaddrinfo(res0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return -1;
    }

    //int broadcast = 1;
    //int ret = setsockopt(client->sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    return 0;
}

static void datagram_client_close(datagram_client_t *client)
{
    if (!client)
        return;

    if (client->sock != INVALID_SOCKET) {
        close(client->sock);
        client->sock = INVALID_SOCKET;
    }

#ifdef _WIN32
    WSACleanup();
#endif 
}

static void datagram_client_send(datagram_client_t *client, const char *message, size_t message_len)
{
    int r =  sendto(client->sock, message, message_len, 0, (struct sockaddr *)&client->addr, client->addr_len);
    if (r == -1) {
        perror("sendto");
    }
}

/* array buffer (string builder) */

typedef struct {
    char *head;
    char *tail;
    size_t left;
} abuf_t;

static void abuf_init(abuf_t *buf, char *dst, size_t len)
{
    buf->head = dst;
    buf->tail = dst;
    buf->left = len;
}

static void abuf_setnull(abuf_t *buf)
{
    buf->head = NULL;
    buf->tail = NULL;
    buf->left = 0;
}

static char *abuf_push(abuf_t *buf)
{
    return buf->tail;
}

static void abuf_pop(abuf_t *buf, char *end)
{
    buf->left += buf->tail - end;
    buf->tail = end;
}

static void abuf_cat(abuf_t *buf, const char *str)
{
    size_t len = strlen(str);
    if (buf->left >= len + 1) {
        strcpy(buf->tail, str);
        buf->tail += len;
        buf->left -= len;
    }
}

static int abuf_printf(abuf_t *buf, const char *restrict format, ...)
{
    va_list ap;
    va_start(ap, format);

    int n = vsnprintf(buf->tail, buf->left, format, ap);
    size_t len = 0;
    if (n > 0) {
        len = (size_t)n < buf->left ? (size_t)n : buf->left;
        buf->tail += len;
        buf->left -= len;
    }
    va_end(ap);
    return n;
}

/* Syslog UDP printer, RFC 5424 (IETF-syslog protocol) */

typedef struct {
    struct data_output output;
    datagram_client_t client;
    int pri;
    char hostname[_POSIX_HOST_NAME_MAX + 1];
    abuf_t msg;
} data_output_syslog_t;

static void print_syslog_array(data_output_t *output, data_array_t *array, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    abuf_cat(&syslog->msg, "[");
    for (int c = 0; c < array->num_values; ++c) {
        if (c)
            abuf_cat(&syslog->msg, ",");
        print_array_value(output, array, format, c);
    }
    abuf_cat(&syslog->msg, "]");
}

static void print_syslog_object(data_output_t *output, data_t *data, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    bool separator = false;
    abuf_cat(&syslog->msg, "{");
    while (data) {
        if (separator)
            abuf_cat(&syslog->msg, ",");
        output->print_string(output, data->key, NULL);
        abuf_cat(&syslog->msg, ":");
        print_value(output, data->type, data->value, data->format);
        separator = true;
        data = data->next;
    }
    abuf_cat(&syslog->msg, "}");
}

static void print_syslog_data(data_output_t *output, data_t *data, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    if (syslog->msg.tail) {
        print_syslog_object(output, data, format);
        return;
    }

    char message[1024];
    abuf_init(&syslog->msg, message, 1024);

    time_t now;
    struct tm tm_info;
    time(&now);
#ifdef _MSC_VER
    gmtime_s(&tm_info, &now);
#else
    gmtime_r(&now, &tm_info);
#endif    
    char timestamp[21];
    strftime(timestamp, 21, "%Y-%m-%dT%H:%M:%SZ", &tm_info);

    abuf_printf(&syslog->msg, "<%d>1 %s %s rtl_433 - - - ", syslog->pri, timestamp, syslog->hostname);

    print_syslog_object(output, data, format);

    datagram_client_send(&syslog->client, message, strlen(message));

    abuf_setnull(&syslog->msg);
}

static void print_syslog_string(data_output_t *output, const char *str, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    char *buf = syslog->msg.tail;
    size_t size = syslog->msg.left;

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

    syslog->msg.tail = buf;
    syslog->msg.left = size;
}

static void print_syslog_double(data_output_t *output, double data, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;
    abuf_printf(&syslog->msg, "%f", data);
}

static void print_syslog_int(data_output_t *output, int data, char *format)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;
    abuf_printf(&syslog->msg, "%d", data);
}

static void data_output_syslog_free(data_output_t *output)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    if (!syslog)
        return;

    datagram_client_close(&syslog->client);

    free(syslog);
}

struct data_output *data_output_syslog_create(const char *host, const char *port)
{
    data_output_syslog_t *syslog = calloc(1, sizeof(data_output_syslog_t));
    if (!syslog) {
        fprintf(stderr, "calloc() failed");
        return NULL;
    }
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2,2),&wsa) != 0) {
        perror("WSAStartup()");
        free(syslog);
        return NULL;
    }
#endif

    syslog->output.print_data   = print_syslog_data;
    syslog->output.print_array  = print_syslog_array;
    syslog->output.print_string = print_syslog_string;
    syslog->output.print_double = print_syslog_double;
    syslog->output.print_int    = print_syslog_int;
    syslog->output.output_free  = data_output_syslog_free;
    // Severity 5 "Notice", Facility 20 "local use 4"
    syslog->pri = 20 * 8 + 5;
    gethostname(syslog->hostname, _POSIX_HOST_NAME_MAX + 1);
    syslog->hostname[_POSIX_HOST_NAME_MAX] = '\0';
    datagram_client_open(&syslog->client, host, port);

    return &syslog->output;
}
