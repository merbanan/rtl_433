/** @file
    InfluxDB output for rtl_433 events.

    Copyright (C) 2019 Daniel Krueger
    based on output_mqtt.c
    Copyright (C) 2019 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

// note: our unit header includes unistd.h for gethostname() via data.h
#include "output_influx.h"
#include "optparse.h"
#include "util.h"
#include "logger.h"
#include "fatal.h"
#include "r_util.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "mongoose.h"

/* InfluxDB client abstraction / printer */

typedef struct {
    struct data_output output;
    struct mg_mgr *mgr;
    struct mg_connection *conn;
    int prev_status;
    int prev_resp_code;
    char hostname[64];
    char url[400];
    char extra_headers[150];
    tls_opts_t tls_opts;
    int databufidxfill;
    struct mbuf databufs[2];

} influx_client_t;

static void influx_client_send(influx_client_t *ctx);

static void influx_client_event(struct mg_connection *nc, int ev, void *ev_data)
{
    // note that while shutting down the ctx is NULL
    influx_client_t *ctx = (influx_client_t *)nc->user_data;
    struct http_message *hm = (struct http_message *)ev_data;

    switch (ev) {
    case MG_EV_CONNECT: {
        int connect_status = *(int *)ev_data;
        if (connect_status != 0) {
            // Error, print only once
            if (ctx) {
                if (ctx->prev_status != connect_status)
                    print_logf(LOG_WARNING, "InfluxDB", "InfluxDB connect error: %s", strerror(connect_status));
                ctx->conn = NULL;
            }
        }
        if (ctx)
            ctx->prev_status = connect_status;
        break;
    }
    case MG_EV_HTTP_CHUNK: // response is normally empty (so mongoose thinks we received a chunk only)
    case MG_EV_HTTP_REPLY:
        nc->flags |= MG_F_CLOSE_IMMEDIATELY;
        if (hm->resp_code == 204) {
            // mark influx data as sent
        }
        else {
            if (ctx && ctx->prev_resp_code != hm->resp_code)
                print_logf(LOG_WARNING, "InfluxDB", "InfluxDB replied HTTP code: %d with message:\n%s", hm->resp_code, hm->body.p);
        }
        if (ctx) {
            ctx->prev_resp_code = hm->resp_code;
        }
        break;
    case MG_EV_CLOSE:
        if (ctx) {
            ctx->conn = NULL;
            influx_client_send(ctx);
        }
        break;
    }
}

static influx_client_t *influx_client_init(influx_client_t *ctx, char const *url, char const *token)
{
    strncpy(ctx->url, url, sizeof(ctx->url));
    ctx->url[sizeof(ctx->url) - 1] = '\0';
    snprintf(ctx->extra_headers, sizeof (ctx->extra_headers), "Authorization: Token %s\r\n", token);

    return ctx;
}

static void influx_client_send(influx_client_t *ctx)
{
    struct mbuf *buf = &ctx->databufs[ctx->databufidxfill];

    /*fprintf(stderr, "Influx %p msg: \"%s\" with %lu/%lu %s\n",
            (void*)ctx, buf->buf, buf->len, buf->size,
            ctx->conn ? "buffering" : "to be sent");*/

    if (ctx->conn || !buf->len)
        return;

    char const *error_string = NULL;
    struct mg_connect_opts opts = {.user_data = ctx, .error_string = &error_string};
    if (ctx->tls_opts.tls_ca_cert) {
        print_logf(LOG_INFO, "InfluxDB", "influxs (TLS) parameters are: "
                                       "tls_cert=%s "
                                       "tls_key=%s "
                                       "tls_ca_cert=%s "
                                       "tls_cipher_suites=%s "
                                       "tls_server_name=%s "
                                       "tls_psk_identity=%s "
                                       "tls_psk_key=%s ",
                ctx->tls_opts.tls_cert,
                ctx->tls_opts.tls_key,
                ctx->tls_opts.tls_ca_cert,
                ctx->tls_opts.tls_cipher_suites,
                ctx->tls_opts.tls_server_name,
                ctx->tls_opts.tls_psk_identity,
                ctx->tls_opts.tls_psk_key);

#if MG_ENABLE_SSL
        opts.ssl_cert          = ctx->tls_opts.tls_cert;
        opts.ssl_key           = ctx->tls_opts.tls_key;
        opts.ssl_ca_cert       = ctx->tls_opts.tls_ca_cert;
        opts.ssl_cipher_suites = ctx->tls_opts.tls_cipher_suites;
        opts.ssl_server_name   = ctx->tls_opts.tls_server_name;
        opts.ssl_psk_identity  = ctx->tls_opts.tls_psk_identity;
        opts.ssl_psk_key       = ctx->tls_opts.tls_psk_key;
#else
        print_log(LOG_FATAL, __func__, "influxs (TLS) not available");
        exit(1);
#endif
    }
    if ((ctx->conn = mg_connect_http_opt(ctx->mgr, influx_client_event, opts, ctx->url, ctx->extra_headers, buf->buf)) == NULL) {
        print_logf(LOG_WARNING, "InfluxDB", "Connect to InfluxDB (%s) failed (%s)", ctx->url, error_string);
    }
    else {
        ctx->databufidxfill ^= 1;
        buf->len = 0;
        *buf->buf = '\0';
    }
}

/* Helper */

/// clean the tag/identifier inplace to [-.A-Za-z0-9], esp. not whitespace, =, comma and replace any leading _ by x
static char *influx_sanitize_tag(char *tag, char *end)
{
    for (char *p = tag; *p && p != end; ++p)
        if (*p != '-' && *p != '.' && (*p < 'A' || *p > 'Z') && (*p < 'a' || *p > 'z') && (*p < '0' || *p > '9'))
            *p = '_';

    for (char *p = tag; *p && p != end; ++p)
        if (*p == '_')
            *p = 'x';
        else
            break;

    return tag;
}

/// reserve additional space of size len at end of mbuf a; returns actual free space
static size_t mbuf_reserve(struct mbuf *a, size_t len)
{
    // insert undefined values at end of current buffer (it will increase the buffer if necessary)
    len = mbuf_insert(a, a->len, NULL, len);
    // reduce the buffer length again by actual inserted number of bytes
    a->len -= len;
    len = a->size - a->len;

    if (len)
        a->buf[a->len] = '\0';

    return len;
}

static char *mbuf_snprintf(struct mbuf *a, char const *format, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
;
static char *mbuf_snprintf(struct mbuf *a, char const *format, ...)
{
    char *str = &a->buf[a->len];
    int size;
    va_list ap;
    va_start(ap, format);
    size = vsnprintf(str, a->size - a->len, format, ap);
    va_end(ap);
    if (size > 0) {
        // vsnprintf might return size larger than actually filled
        size_t len = strlen(str);
        a->len += len;
    }
    return str;
}

static void mbuf_remove_part(struct mbuf *a, char *pos, size_t len)
{
    if (pos >= a->buf && pos < &a->buf[a->len] && &pos[len] <= &a->buf[a->len]) {
        memmove(pos, &pos[len], a->len - (pos - a->buf) - len);
        a->len -= len;
    }
}

static void R_API_CALLCONV print_influx_array(data_output_t *output, data_array_t *array, char const *format)
{
    UNUSED(array);
    UNUSED(format);
    influx_client_t *influx = (influx_client_t *)output;
    struct mbuf *buf = &influx->databufs[influx->databufidxfill];
    mbuf_snprintf(buf, "\"array\""); // TODO
}

static void R_API_CALLCONV print_influx_data_escaped(data_output_t *output, data_t *data, char const *format)
{
    char str[1000];
    data_print_jsons(data, str, sizeof (str));
    output->print_string(output, str, format);
}

static void R_API_CALLCONV print_influx_string_escaped(data_output_t *output, char const *str, char const *format)
{
    UNUSED(format);
    influx_client_t *influx = (influx_client_t *)output;
    struct mbuf *databuf = &influx->databufs[influx->databufidxfill];
    size_t size = databuf->size - databuf->len;
    char *buf = &databuf->buf[databuf->len];

    if (size < strlen(str) + 3) {
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

    databuf->len = databuf->size - size;
}

static void R_API_CALLCONV print_influx_string(data_output_t *output, char const *str, char const *format)
{
    UNUSED(format);
    influx_client_t *influx = (influx_client_t *)output;
    struct mbuf *buf = &influx->databufs[influx->databufidxfill];
    mbuf_snprintf(buf, "%s", str);
}

// Generate InfluxDB line protocol
static void R_API_CALLCONV print_influx_data(data_output_t *output, data_t *data, char const *format)
{
    UNUSED(format);
    influx_client_t *influx = (influx_client_t *)output;
    char *str;
    char *end;
    struct mbuf *buf = &influx->databufs[influx->databufidxfill];
    bool comma = false;

    data_t *data_org = data;
    data_t *data_model = NULL;
    data_t *data_time = NULL;
    for (data_t *d = data; d; d = d->next) {
        if (!strcmp(d->key, "model"))
            data_model = d;
        if (!strcmp(d->key, "time"))
            data_time = d;
    }

    if (!data_model) {
        // data isn't from device (maybe report for example)
        // use hostname for measurement

        mbuf_reserve(buf, 20000);
        mbuf_snprintf(buf, "rtl_433_%s", influx->hostname);
    }
    else {
        // use model for measurement

        mbuf_reserve(buf, 1000);
        str = &buf->buf[buf->len];
        print_value(output, data_model->type, data_model->value, data_model->format);
        influx_sanitize_tag(str, NULL);
    }

    // write tags
    while (data) {
        if (!strcmp(data->key, "model")
                || !strcmp(data->key, "time")) {
            // skip
        }
        else if (!strcmp(data->key, "type")
                || !strcmp(data->key, "subtype")
                || !strcmp(data->key, "id")
                || !strcmp(data->key, "channel")
                || !strcmp(data->key, "mic")) {
            str = mbuf_snprintf(buf, ",%s=", data->key);
            str++;
            end = &buf->buf[buf->len - 1];
            influx_sanitize_tag(str, end);
            str = end + 1;
            print_value(output, data->type, data->value, data->format);
            influx_sanitize_tag(str, NULL);
        }
        data = data->next;
    }

    mbuf_snprintf(buf, " ");

    // activate escaped output functions
    influx->output.print_data   = print_influx_data_escaped;
    influx->output.print_string = print_influx_string_escaped;

    // write fields
    data = data_org;
    while (data) {
        if (!strcmp(data->key, "model")
                || !strcmp(data->key, "time")) {
            // skip
        }
        else if (!strcmp(data->key, "type")
                || !strcmp(data->key, "subtype")
                || !strcmp(data->key, "id")
                || !strcmp(data->key, "channel")
                || !strcmp(data->key, "mic")) {
            // skip
        }
        else {
            str = mbuf_snprintf(buf, comma ? ",%s=" : "%s=", data->key);
            if (comma)
                str++;
            end = &buf->buf[buf->len - 1];
            influx_sanitize_tag(str, end);
            str = end + 1;
            print_value(output, data->type, data->value, data->format);
            comma = true;
        }
        data = data->next;
    }

    // restore original output functions
    influx->output.print_data   = print_influx_data;
    influx->output.print_string = print_influx_string;

    // write time if available
    if (data_time) {
        str = mbuf_snprintf(buf, " ");
        print_value(output, data_time->type, data_time->value, data_time->format);
        if (str[1] == '@' // relative time format configured
                || str[11] == ' ' // date time format configured
                || str[11] == 'T') {  // ISO date time format configured
            // -> bad, because InfluxDB doesn't under stand those formats -> remove timestamp
            buf->len = str - buf->buf;
        }
        else if ((str = strchr(str, '.'))) {
            // unix usec timestamp format configured
            mbuf_remove_part(buf, str, 1);
            mbuf_snprintf(buf, "000");
        }
        else {
            // unix timestamp with seconds resolution configured
            mbuf_snprintf(buf, "000000000");
        }
    }
    mbuf_snprintf(buf, "\n");

    influx_client_send(influx);
}

static void R_API_CALLCONV print_influx_double(data_output_t *output, double data, char const *format)
{
    UNUSED(format);
    influx_client_t *influx = (influx_client_t *)output;
    struct mbuf *buf = &influx->databufs[influx->databufidxfill];
    mbuf_snprintf(buf, "%f", data);
}

static void R_API_CALLCONV print_influx_int(data_output_t *output, int data, char const *format)
{
    UNUSED(format);
    influx_client_t *influx = (influx_client_t *)output;
    struct mbuf *buf = &influx->databufs[influx->databufidxfill];
    mbuf_snprintf(buf, "%d", data);
}

static void R_API_CALLCONV data_output_influx_free(data_output_t *output)
{
    influx_client_t *influx = (influx_client_t *)output;

    if (!influx)
        return;

    // remove ctx from our connections
    if (influx->conn) {
        influx->conn->user_data = NULL;
        influx->conn->flags |= MG_F_CLOSE_IMMEDIATELY;
    }

    free(influx);
}

struct data_output *data_output_influx_create(struct mg_mgr *mgr, char *opts)
{
    influx_client_t *influx = calloc(1, sizeof(influx_client_t));
    if (!influx) {
        FATAL_CALLOC("data_output_influx_create()");
    }

    gethostname(influx->hostname, sizeof(influx->hostname) - 1);
    influx->hostname[sizeof(influx->hostname) - 1] = '\0';
    // only use hostname, not domain part
    char *dot = strchr(influx->hostname, '.');
    if (dot)
        *dot = '\0';
    influx_sanitize_tag(influx->hostname, NULL);

    char *token = NULL;

    // param/opts starts with URL
    char *url = opts;
    opts = strchr(opts, ',');
    if (opts) {
        *opts = '\0';
        opts++;
    }
    if (strncmp(url, "influx", 6) == 0) {
        url += 2;
        memcpy(url, "http", 4);
    }
    if (strncmp(url, "https", 5) == 0) {
        influx->tls_opts.tls_ca_cert = "*"; // TLS is enabled but no cert verification is performed.
    }

    // check if valid URL has been provided
    struct mg_str host, path, query;
    if (mg_parse_uri(mg_mk_str(url), NULL, NULL, &host, NULL, &path,
                &query, NULL) != 0
            || !host.len || !path.len || !query.len) {
        print_logf(LOG_FATAL, __func__, "Invalid URL to InfluxDB specified.%s%s%s"
                        " Something like \"influx://<host>/write?org=<org>&bucket=<bucket>\" required at least.",
                !host.len ? " No host specified." : "",
                !path.len ? " No path component specified." : "",
                !query.len ? " No query parameters specified." : "");
        exit(1);
    }

    // parse auth and format options
    char *key, *val;
    while (getkwargs(&opts, &key, &val)) {
        key = remove_ws(key);
        val = trim_ws(val);
        if (!key || !*key)
            continue;
        else if (!strcasecmp(key, "t") || !strcasecmp(key, "token"))
            token = val;
        else if (!tls_param(&influx->tls_opts, key, val)) {
            // ok
        }
        else {
            print_logf(LOG_FATAL, __func__, "Invalid key \"%s\" option.", key);
            exit(1);
        }
    }

    influx->output.print_data   = print_influx_data;
    influx->output.print_array  = print_influx_array;
    influx->output.print_string = print_influx_string;
    influx->output.print_double = print_influx_double;
    influx->output.print_int    = print_influx_int;
    influx->output.output_free  = data_output_influx_free;

    print_logf(LOG_CRITICAL, "InfluxDB", "Publishing data to InfluxDB (%s)", url);

    influx->mgr = mgr;
    influx_client_init(influx, url, token);

    return &influx->output;
}
