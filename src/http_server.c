/**
 * HTTP control interface
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
# HTTP control interface

A choice of endpoints are available:
- "/": serves a user interface (currently redirected to hosted app)
    (also serves "/favicon.ico", "/app.css", "/app.js", "/vendor.css", "/vendor.js")
- "/jsonrpc": JSON-RPC API
- "/cmd": simple JSON command API
- "/events": HTTP (chunked) streaming API, streams JSON events
- "/stream": HTTP (plain) streaming API, streams JSON events
- "/api": RESTful API (not implemented)
- "ws:": Websocket API (similar to cmd/events API)

## JSON-RPC API

S.a. https://www.jsonrpc.org/specification

Examples:

    {"jsonrpc": "2.0", "method": "sample_rate", "params": [1024000], "id": 0}
    {"jsonrpc": "2.0", "result": "Ok", "id": 0}
    {"jsonrpc": "2.0", "error": {"code": -32600, "message": "Invalid Request"}, "id": null}

## JSON command / Websocket API

Simplified JSON command and query.

Examples:

    {"cmd": "sample_rate", "val": 1024000}
    {"result": "Ok"}
    {"error": "Invalid Request"}}

## HTTP events / streaming / Websocket API

You will receive JSON events, one per line terminated with CRLF.
On Events and Stream endpoints a keep-alive of CRLF will be send every 60 seconds.
Use e.g. httpie with `http --stream --timeout=70 :8433/events`
or `(echo "GET /stream HTTP/1.0\n"; sleep 600) | socat - tcp:127.0.0.1:8433`

## Queries

- "registered_protocols"
- "enabled_protocols"
- "protocol_info"
    .name
    .modulation
    .short_width
    .long_width
    .sync_width
    .tolerance
    .gap_limit
    .reset_limit
    .fields

- "device_info"
    device  0:  Realtek, RTL2838UHIDIR, SN: 00000001
    Found Rafael Micro R820T tuner
    Using device 0: Generic RTL2832U OEM

- "settings"
    "device":           0
    "gain":             0
    "center_frequency": 433920000
    "hop_interval":     600
    "ppm_error":        0
    "sample_rate":      250000
    "report_meta":      ["time", "reltime", "notime", "hires", "utc", "protocol", "level"]
    "convert":          "native"|"si"|"customary"

## Commands

- "device":           0
- "gain":             0
- "center_frequency": 433920000
- "hop_interval":     600
- "ppm_error":        0
- "sample_rate":      250000
- "report_meta":      "time"|"reltime"|"notime"|"hires"|"utc"|"protocol"|"level"
- "convert":          "native"|"si"|"customary"
- "protocol":         1

*/

#include "http_server.h"
#include "data.h"
#include "rtl_433.h"
#include "r_api.h"
#include "r_device.h" // used for protocols
#include "r_private.h" // used for protocols
#include "r_util.h"
#include "optparse.h"
#include "abuf.h"
#include "list.h" // used for protocols
#include "jsmn.h"
#include "mongoose.h"
#include "logger.h"
#include "fatal.h"
#include <stdbool.h>

// embed index.html so browsers allow access as local
#define INDEX_HTML \
    "<!DOCTYPE html>" \
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" \
    "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">" \
    "<link rel=\"icon\" href=\"https://triq.org/rxui/favicon.ico\">" \
    "<title>rxui</title>" \
    "<link href=\"https://fonts.googleapis.com/css?family=Roboto:100,300,400,500,700,900|Material+Icons\" rel=\"stylesheet\">" \
    "<link href=\"https://triq.org/rxui/css/app.css\" rel=\"preload\" as=\"style\">" \
    "<link href=\"https://triq.org/rxui/css/chunk-vendors.css\" rel=\"preload\" as=\"style\">" \
    "<link href=\"https://triq.org/rxui/js/app.js\" rel=\"preload\" as=\"script\">" \
    "<link href=\"https://triq.org/rxui/js/chunk-vendors.js\" rel=\"preload\" as=\"script\">" \
    "<link href=\"https://triq.org/rxui/css/chunk-vendors.css\" rel=\"stylesheet\">" \
    "<link href=\"https://triq.org/rxui/css/app.css\" rel=\"stylesheet\">" \
    "<div id=\"app\"></div>" \
    "<noscript><strong>We're sorry but rxui doesn't work properly without JavaScript enabled. Please enable it to continue.</strong></noscript>" \
    "<script src=\"https://triq.org/rxui/js/chunk-vendors.js\"></script>" \
    "<script src=\"https://triq.org/rxui/js/app.js\"></script>"

// generic ring list

#define DEFAULT_HISTORY_SIZE 100

typedef struct {
    unsigned size;
    void **data;
    void **head;
    void **tail;
} ring_list_t;

static ring_list_t *ring_list_new(unsigned size)
{
    ring_list_t *ring = calloc(1, sizeof(ring_list_t));
    if (!ring) {
        WARN_CALLOC("ring_list_new()");
        return NULL;
    }

    ring->data = calloc(size, sizeof(void *));
    if (!ring->data) {
        WARN_CALLOC("ring_list_new()");
        free(ring);
        return NULL;
    }

    ring->size = size;
    ring->tail = ring->data;

    return ring;
}

// the ring needs to be empty before calling this
static void ring_list_free(ring_list_t *ring)
{
    if (ring) {
        if (ring->data)
            free(ring->data);
        free(ring);
    }
}

// free the data returned
static void *ring_list_shift(ring_list_t *ring)
{
    if (!ring->head)
        return NULL;

    void *ret = *ring->head;

    ++ring->head;
    if (ring->head >= ring->data + ring->size)
        ring->head -= ring->size;
    if (ring->head == ring->tail)
        ring->head = NULL;

    return ret;
}

// retain data before passing in and free the data returned.
static void *ring_list_push(ring_list_t *ring, void *data)
{
    *ring->tail = data;

    if (!ring->head)
        ring->head = ring->tail;

    ++ring->tail;
    if (ring->tail >= ring->data + ring->size)
        ring->tail -= ring->size;

    if (ring->tail == ring->head)
        return ring_list_shift(ring);

    return NULL;
}

static void **ring_list_iter(ring_list_t *ring)
{
    return ring->head;
}

static void **ring_list_next(ring_list_t *ring, void **iter)
{
    if (!iter)
        return NULL;

    ++iter;
    if (iter >= ring->data + ring->size)
        iter -= ring->size;
    if (iter == ring->tail)
        iter = NULL;

    return iter;
}

// data helpers that could go into r_api

static data_t *meta_data(r_cfg_t *cfg)
{
    return data_make(
            "frequencies", "", DATA_ARRAY, data_array(cfg->frequencies, DATA_INT, cfg->frequency),
            "hop_times", "", DATA_ARRAY, data_array(cfg->hop_times, DATA_INT, cfg->hop_time),
            "center_frequency", "", DATA_INT, cfg->center_frequency,
            "duration", "", DATA_INT, cfg->duration,
            "samp_rate", "", DATA_INT, cfg->samp_rate,
            "conversion_mode", "", DATA_INT, cfg->conversion_mode,
            "fsk_pulse_detect_mode", "", DATA_INT, cfg->fsk_pulse_detect_mode,
            "after_successful_events_flag", "", DATA_INT, cfg->after_successful_events_flag,
            "report_meta", "", DATA_INT, cfg->report_meta,
            "report_protocol", "", DATA_INT, cfg->report_protocol,
            "report_time", "", DATA_INT, cfg->report_time,
            "report_time_hires", "", DATA_INT, cfg->report_time_hires,
            "report_time_tz", "", DATA_INT, cfg->report_time_tz,
            "report_time_utc", "", DATA_INT, cfg->report_time_utc,
            "report_description", "", DATA_INT, cfg->report_description,
            "report_stats", "", DATA_INT, cfg->report_stats,
            "stats_interval", "", DATA_INT, cfg->stats_interval,
            NULL);
}

static data_t *protocols_data(r_cfg_t *cfg)
{
    list_t devs = {0};
    list_ensure_size(&devs, cfg->num_r_devices);

    for (int i = 0; i < cfg->num_r_devices; ++i) {
        r_device *dev = &cfg->devices[i];

        int enabled = 0;
        for (void **iter = cfg->demod->r_devs.elems; iter && *iter; ++iter) {
            r_device *r_dev = *iter;
            if (r_dev->protocol_num == dev->protocol_num) {
                enabled = 1;
                break;
            }
        }
        int fields_len = 0;
        for (char const *const *iter = dev->fields; iter && *iter; ++iter) {
            fields_len++;
        }
        data_t *data = data_make(
                "num", "", DATA_INT, dev->protocol_num,
                "name", "", DATA_STRING, dev->name,
                "mod", "", DATA_INT, dev->modulation,
                "short", "", DATA_DOUBLE, dev->short_width,
                "long", "", DATA_DOUBLE, dev->long_width,
                "reset", "", DATA_DOUBLE, dev->reset_limit,
                "gap", "", DATA_DOUBLE, dev->gap_limit,
                "sync", "", DATA_DOUBLE, dev->sync_width,
                "tolerance", "", DATA_DOUBLE, dev->tolerance,
                "fields", "", DATA_ARRAY, data_array(fields_len, DATA_STRING, dev->fields),
                "def", "", DATA_INT, dev->disabled == 0,
                "en", "", DATA_INT, enabled,
                "verbose", "", DATA_INT, dev->verbose,
                "verbose_bits", "", DATA_INT, dev->verbose_bits,
                NULL);
        list_push(&devs, data);
    }

    for (void **iter = cfg->demod->r_devs.elems; iter && *iter; ++iter) {
        r_device *dev = *iter;
        if (dev->protocol_num > 0) {
                continue;
        }
        int fields_len = 0;
        for (char const *const *iter2 = dev->fields; iter2 && *iter2; ++iter2) {
            fields_len++;
        }
        data_t *data = data_make(
                "name", "", DATA_STRING, dev->name,
                "mod", "", DATA_INT, dev->modulation,
                "short", "", DATA_DOUBLE, dev->short_width,
                "long", "", DATA_DOUBLE, dev->long_width,
                "reset", "", DATA_DOUBLE, dev->reset_limit,
                "gap", "", DATA_DOUBLE, dev->gap_limit,
                "sync", "", DATA_DOUBLE, dev->sync_width,
                "tolerance", "", DATA_DOUBLE, dev->tolerance,
                "fields", "", DATA_ARRAY, data_array(fields_len, DATA_STRING, dev->fields),
                "en", "", DATA_INT, 1,
                "verbose", "", DATA_INT, dev->verbose,
                "verbose_bits", "", DATA_INT, dev->verbose_bits,
                NULL);
        list_push(&devs, data);
    }

    data_t *data = data_make(
            "protocols", "", DATA_ARRAY, data_array(devs.len, DATA_DATA, devs.elems),
            NULL);
    list_free_elems(&devs, NULL);
    return data;
}

// very narrowly tailored JSON parsing

typedef struct rpc rpc_t;

typedef void (*rpc_response_fn)(rpc_t *rpc, int error_code, char const *message, int is_json);

struct rpc {
    struct mg_connection *nc;
    rpc_response_fn response;
    int ver;
    char *method;
    char *arg;
    uint32_t val;
    //list_t params;
    char *id;
};

static int jsoneq(const char *json, jsmntok_t *tok, const char *s)
{
    if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
            strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
        return 0;
    }
    return -1;
}

static char *jsondup(const char *json, jsmntok_t *tok)
{
    int len = tok->end - tok->start;
    char *p = malloc(len + 1);
    if (!p) {
        WARN_MALLOC("jsondup()");
        return NULL;
    }
    p[len] = '\0';
    return memcpy(p, json + tok->start, len);
}

static char *jsondupq(const char *json, jsmntok_t *tok)
{
    int len = tok->end - tok->start + 2;
    char *p = malloc(len + 1);
    if (!p) {
        WARN_MALLOC("jsondupq()");
        return NULL;
    }
    p[len] = '\0';
    return memcpy(p, json + tok->start - 1, len);
}

// {"cmd": "report_meta", "arg": "utc", "val": 1}
static int json_parse(rpc_t *rpc, struct mg_str const *json)
{
    int i;
    int r;
    jsmn_parser p;
    jsmntok_t t[16]; /* We expect no more than 7 tokens */

    char *cmd    = NULL;
    char *arg    = NULL;
    uint32_t val = 0;

    jsmn_init(&p);
    r = jsmn_parse(&p, json->p, json->len, t, sizeof(t) / sizeof(t[0]));
    if (r < 0) {
        print_logf(LOG_WARNING, __func__, "Failed to parse JSON: %d", r);
        return -1;
    }

    /* Assume the top-level element is an object */
    if (r < 1 || t[0].type != JSMN_OBJECT) {
        print_log(LOG_WARNING, __func__, "Object expected");
        return -1;
    }

    /* Loop over all keys of the root object */
    for (i = 1; i < r; i++) {
        if (jsoneq(json->p, &t[i], "cmd") == 0) {
            i++;
            free(cmd);
            cmd = jsondup(json->p, &t[i]);
        }
        else if (jsoneq(json->p, &t[i], "arg") == 0) {
            i++;
            free(arg);
            arg = jsondup(json->p, &t[i]);
        }
        else if (jsoneq(json->p, &t[i], "val") == 0) {
            i++;
            char *endptr = NULL;
            val = strtol(json->p + t[i].start, &endptr, 10);
            // compare endptr to t[i].end
        }
        else {
            print_logf(LOG_WARNING, __func__, "Unexpected key: %.*s", t[i].end - t[i].start, json->p + t[i].start);
        }
    }

    if (!cmd) {
        free(arg);
        return -1;
    }
    rpc->method     = cmd;
    rpc->arg        = arg;
    rpc->val        = val;
    return 0;
}

// {"jsonrpc": "2.0", "method": "report_meta", "params": ["utc", 1], "id": 0}
static int jsonrpc_parse(rpc_t *rpc, struct mg_str const *json)
{
    int r;
    jsmn_parser p;
    jsmntok_t t[16]; /* We expect no more than 11 tokens */

    char *cmd    = NULL;
    char *id     = NULL;
    char *arg    = NULL;
    uint32_t val = 0;

    jsmn_init(&p);
    r = jsmn_parse(&p, json->p, json->len, t, sizeof(t) / sizeof(t[0]));
    if (r < 0) {
        print_logf(LOG_WARNING, __func__, "Failed to parse JSON: %d", r);
        return -1;
    }

    /* Assume the top-level element is an object */
    if (r < 1 || t[0].type != JSMN_OBJECT) {
        print_log(LOG_WARNING, __func__, "Object expected");
        return -1;
    }

    /* Loop over all keys of the root object */
    for (int i = 1; i < r; i++) {
        if (jsoneq(json->p, &t[i], "jsonrpc") == 0) {
            i++;
            // (jsoneq(json->p, &t[i], "2.0") == 0);
        }
        else if (jsoneq(json->p, &t[i], "method") == 0) {
            i++;
            free(cmd);
            cmd = jsondup(json->p, &t[i]);
        }
        else if (jsoneq(json->p, &t[i], "id") == 0) {
            i++;
            if (t[i].type == JSMN_STRING) {
                free(id);
                id = jsondupq(json->p, &t[i]);
            }
            else if (t[i].type == JSMN_PRIMITIVE) {
                free(id);
                id = jsondup(json->p, &t[i]);
            }
        }
        else if (jsoneq(json->p, &t[i], "params") == 0) {
            //printf("- Params:\n");
            if (t[i + 1].type != JSMN_ARRAY) {
                continue; /* We expect groups to be an array of strings */
            }
            for (int j = 0; j < t[i + 1].size; j++) {
                jsmntok_t *g = &t[i + j + 2];
                if (g->type == JSMN_STRING) {
                    free(arg);
                    arg = jsondup(json->p, g);
                }
                else if (g->type == JSMN_PRIMITIVE) {
                    // Number, null/true/false not supported
                    char *endptr = NULL;
                    val          = strtol(json->p + g->start, &endptr, 10);
                }
                //printf("  * %.*s\n", g->end - g->start, json + g->start);
            }
            i += t[i + 1].size + 1;
        }
        else {
            print_logf(LOG_WARNING, __func__, "Unexpected key: %.*s", t[i].end - t[i].start, json->p + t[i].start);
        }
    }

    if (!cmd) {
        free(id);
        free(arg);
        return -1;
    }
    rpc->method  = cmd;
    rpc->arg     = arg;
    rpc->val     = val;
    rpc->id      = id;
    return 0;
}

static void rpc_exec(rpc_t *rpc, r_cfg_t *cfg)
{
    if (!rpc || !rpc->method || !*rpc->method) {
        rpc->response(rpc, -1, "Method invalid", 0);
    }
    // Getter
    else if (!strcmp(rpc->method, "get_dev_query")) {
        rpc->response(rpc, 0, cfg->dev_query, 0);
    }
    else if (!strcmp(rpc->method, "get_dev_info")) {
        rpc->response(rpc, 1, cfg->dev_info, 0);
    }
    else if (!strcmp(rpc->method, "get_gain")) {
        rpc->response(rpc, 0, cfg->gain_str, 0);
    }

    else if (!strcmp(rpc->method, "get_ppm_error")) {
        rpc->response(rpc, 2, NULL, cfg->ppm_error);
    }
    else if (!strcmp(rpc->method, "get_hop_interval")) {
        rpc->response(rpc, 2, NULL, cfg->hop_time[0]);
    }
    else if (!strcmp(rpc->method, "get_center_frequency")) {
        rpc->response(rpc, 3, NULL, cfg->center_frequency); // unsigned
    }
    else if (!strcmp(rpc->method, "get_sample_rate")) {
        rpc->response(rpc, 3, NULL, cfg->samp_rate); // unsigned
    }
    else if (!strcmp(rpc->method, "get_grab_mode")) {
        rpc->response(rpc, 2, NULL, cfg->grab_mode);
    }
    else if (!strcmp(rpc->method, "get_raw_mode")) {
        rpc->response(rpc, 2, NULL, cfg->raw_mode);
    }
    else if (!strcmp(rpc->method, "get_verbosity")) {
        rpc->response(rpc, 2, NULL, cfg->verbosity);
    }
    else if (!strcmp(rpc->method, "get_verbose_bits")) {
        rpc->response(rpc, 2, NULL, cfg->verbose_bits);
    }
    else if (!strcmp(rpc->method, "get_conversion_mode")) {
        rpc->response(rpc, 2, NULL, cfg->conversion_mode);
    }
    else if (!strcmp(rpc->method, "get_stats")) {
        char buf[20480]; // we expect the stats string to be around 15k bytes.
        data_t *data = create_report_data(cfg, 2/*report active devices*/);
        // flush_report_data(cfg); // snapshot, do not flush
        data_print_jsons(data, buf, sizeof(buf));
        rpc->response(rpc, 1, buf, 0);
        data_free(data);
    }
    else if (!strcmp(rpc->method, "get_meta")) {
        char buf[2048]; // we expect the meta string to be around 500 bytes.
        data_t *data = meta_data(cfg);
        data_print_jsons(data, buf, sizeof(buf));
        rpc->response(rpc, 1, buf, 0);
        data_free(data);
    }
    else if (!strcmp(rpc->method, "get_protocols")) {
        char buf[65536]; // we expect the protocol string to be around 60k bytes.
        data_t *data = protocols_data(cfg);
        data_print_jsons(data, buf, sizeof(buf));
        rpc->response(rpc, 1, buf, 0);
        data_free(data);
    }

    // Setter
    else if (!strcmp(rpc->method, "hop_interval")) {
        cfg->hop_time[0] = rpc->val;
        rpc->response(rpc, 0, "Ok", 0);
    }
    else if (!strcmp(rpc->method, "report_meta")) {
        if (!rpc->arg)
            rpc->response(rpc, -1, "Missing arg", 0);
        else if (!strcasecmp(rpc->arg, "time"))
            cfg->report_time = REPORT_TIME_DATE;
        else if (!strcasecmp(rpc->arg, "reltime"))
            cfg->report_time = REPORT_TIME_SAMPLES;
        else if (!strcasecmp(rpc->arg, "notime"))
            cfg->report_time = REPORT_TIME_OFF;
        else if (!strcasecmp(rpc->arg, "hires"))
            cfg->report_time_hires = rpc->val;
        else if (!strcasecmp(rpc->arg, "utc"))
            cfg->report_time_utc = rpc->val;
        else if (!strcasecmp(rpc->arg, "protocol"))
            cfg->report_protocol = rpc->val;
        else if (!strcasecmp(rpc->arg, "level"))
            cfg->report_meta = rpc->val;
        else if (!strcasecmp(rpc->arg, "bits"))
            cfg->verbose_bits = rpc->val;
        else if (!strcasecmp(rpc->arg, "description"))
            cfg->report_description = rpc->val;
        else
            cfg->report_meta = rpc->val;
        rpc->response(rpc, 0, "Ok", 0);
    }
    else if (!strcmp(rpc->method, "convert")) {
        cfg->conversion_mode = rpc->val;
        rpc->response(rpc, 0, "Ok", 0);
    }
    else if (!strcmp(rpc->method, "raw_mode")) {
        cfg->raw_mode = rpc->val;
        rpc->response(rpc, 0, "Ok", 0);
    }
    else if (!strcmp(rpc->method, "verbosity")) {
        cfg->verbosity = rpc->val;
        rpc->response(rpc, 0, "Ok", 0);
    }
    else if (!strcmp(rpc->method, "verbose_bits")) {
        cfg->verbose_bits = rpc->val;
        rpc->response(rpc, 0, "Ok", 0);
    }
    else if (!strcmp(rpc->method, "protocol")) {
        // set_protocol(rpc->val);
        rpc->response(rpc, 0, "Ok", 0);
    }

    // Apply
    else if (!strcmp(rpc->method, "device")) {
        if (!rpc->arg)
            rpc->response(rpc, -1, "Missing arg", 0);
        /*
        if (cfg->set_dev_query)
            rpc->response(rpc, -1, "Try again later", 0);
        cfg->set_dev_query = strdup(rpc->arg);
        if (!cfg->set_dev_query) {
            WARN_STRDUP("rpc_exec()");
        }
        */
        rpc->response(rpc, -1, "Not implemented", 0);
    }
    else if (!strcmp(rpc->method, "gain")) {
        if (!rpc->arg)
            rpc->response(rpc, -1, "Missing arg", 0);
        set_gain_str(cfg, rpc->arg);
        rpc->response(rpc, 0, "Ok", 0);
    }
    else if (!strcmp(rpc->method, "center_frequency")) {
        set_center_freq(cfg, rpc->val);
        rpc->response(rpc, 0, "Ok", 0);
    }
    else if (!strcmp(rpc->method, "ppm_error")) {
        set_freq_correction(cfg, rpc->val);
        rpc->response(rpc, 0, "Ok", 0);
    }
    else if (!strcmp(rpc->method, "sample_rate")) {
        set_sample_rate(cfg, rpc->val);
        rpc->response(rpc, 0, "Ok", 0);
    }

    // Invalid
    else {
        rpc->response(rpc, -1, "Unknown method", 0);
    }
}

// http server

#define KEEP_ALIVE 60 /* seconds */

struct http_server_context {
    struct mg_connection *conn;
    struct mg_serve_http_opts server_opts;
    r_cfg_t *cfg;
    struct data_output *output;
    ring_list_t *history;
};

struct nc_context {
    int is_chunked;
};

static void handle_options(struct mg_connection *nc, struct http_message *hm)
{
    UNUSED(hm);
    mg_printf(nc,
            "HTTP/1.1 204 No Content\r\n"
            "Content-Length: 0\r\n"
            "Cache-Control: max-age=0, private, must-revalidate\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Expose-Headers:\r\n"
            "Access-Control-Allow-Credentials: true\r\n"
            "Access-Control-Max-Age: 1728000\r\n"
            "Access-Control-Allow-Headers: Authorization,Content-Type,Accept,Origin,User-Agent,DNT,Cache-Control,X-Mx-ReqToken,Keep-Alive,X-Requested-With,If-Modified-Since,X-CSRF-Token\r\n"
            "Access-Control-Allow-Methods: GET,POST,PUT,PATCH,DELETE,OPTIONS\r\n"
            "\r\n");
}

static void handle_get(struct mg_connection *nc, struct http_message *hm, char const *buf, unsigned int len)
{
    UNUSED(hm);
    //mg_send_head(nc, 200, -1, NULL);
    mg_printf(nc,
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: %u\r\n"
            "\r\n", len);
    mg_send(nc, buf, (size_t)len);
}

static void handle_redirect(struct mg_connection *nc, struct http_message *hm)
{
    // get the host header
    struct mg_str host = {0};
    for (int i = 0; i < MG_MAX_HTTP_HEADERS && hm->header_names[i].len > 0; i++) {
        // struct mg_str hn = hm->header_names[i];
        // struct mg_str hv = hm->header_values[i];
        // fprintf(stderr, "Header: %.*s: %.*s\n", (int)hn.len, hn.p, (int)hv.len, hv.p);
        if (mg_vcasecmp(&hm->header_names[i], "Host") == 0) {
            host = hm->header_values[i];
            break;
        }
    }

    mg_printf(nc, "%s%s%.*s%s\r\n",
            "HTTP/1.1 307 Temporary Redirect\r\n",
            "Location: http://triq.org/rxui/#",
            (int)host.len, host.p,
            "\r\n\r\n");
}

// reply to ws command
static void rpc_response_ws(rpc_t *rpc, int ret_code, char const *message, int arg)
{
    if (ret_code < 0) {
        mg_printf_websocket_frame(rpc->nc, WEBSOCKET_OP_TEXT,
                "{\"error\": {\"code\": %d, \"message\": \"%s\"}}",
                ret_code, message);
    }
    else if (ret_code == 0 && message) {
        mg_printf_websocket_frame(rpc->nc, WEBSOCKET_OP_TEXT,
                "{\"result\": \"%s\"}",
                message);
    }
    else if (ret_code == 0) {
        mg_printf_websocket_frame(rpc->nc, WEBSOCKET_OP_TEXT,
                "{\"result\": null}");
    }
    else if (ret_code == 1) {
        mg_send_websocket_frame(rpc->nc, WEBSOCKET_OP_TEXT, message, strlen(message));
    }
    else if (ret_code == 2) {
        mg_printf_websocket_frame(rpc->nc, WEBSOCKET_OP_TEXT,
                "{\"result\": %d}",
                arg);
    }
    else /* if (ret_code == 3) */ {
        mg_printf_websocket_frame(rpc->nc, WEBSOCKET_OP_TEXT,
                "{\"result\": %u}",
                (unsigned)arg);
    }
}

// reply to jsonrpc command
static void rpc_response_jsonrpc(rpc_t *rpc, int ret_code, char const *message, int arg)
{
    char const *id = rpc->id ? rpc->id : "null";
    if (ret_code < 0) {
        mg_printf_http_chunk(rpc->nc,
                "{\"jsonrpc\": \"2.0\", \"error\": {\"code\": %d, \"message\": \"%s\"}, \"id\": %s}",
                ret_code, message, id);
    }
    else if (ret_code == 0 && message) {
        mg_printf_http_chunk(rpc->nc,
                "{\"jsonrpc\": \"2.0\", \"result\": \"%s\", \"id\": %s}",
                message, id);
    }
    else if (ret_code == 0) {
        mg_printf_http_chunk(rpc->nc,
                "{\"jsonrpc\": \"2.0\", \"result\": null, \"id\": %s}",
                id);
    }
    else if (ret_code == 1) {
        mg_printf_http_chunk(rpc->nc,
                "{\"jsonrpc\": \"2.0\", \"result\": %s, \"id\": %s}",
                message, id);
    }
    else if (ret_code == 2) {
        mg_printf_http_chunk(rpc->nc,
                "{\"jsonrpc\": \"2.0\", \"result\": %d, \"id\": %s}",
                arg, id);
    }
    else /* if (ret_code == 3) */ {
        mg_printf_http_chunk(rpc->nc,
                "{\"jsonrpc\": \"2.0\", \"result\": %u, \"id\": %s}",
                (unsigned)arg, id);
    }
    mg_send_http_chunk(rpc->nc, "", 0); /* Send empty chunk, the end of response */
}

// reply to json command
static void rpc_response_jsoncmd(rpc_t *rpc, int ret_code, char const *message, int arg)
{
    if (ret_code < 0) {
        mg_printf_http_chunk(rpc->nc,
                "{\"error\": {\"code\": %d, \"message\": \"%s\"}}",
                ret_code, message);
    }
    else if (ret_code == 0 &&message) {
        mg_printf_http_chunk(rpc->nc,
                "{\"result\": \"%s\"}",
                message);
    }
    else if (ret_code == 0) {
        mg_printf_http_chunk(rpc->nc,
                "{\"result\": null}");
    }
    else if (ret_code == 1) {
        mg_printf_http_chunk(rpc->nc,
                "{\"result\": %s}",
                message);
    }
    else if (ret_code == 2) {
        mg_printf_http_chunk(rpc->nc,
                "{\"result\": %d}",
                arg);
    }
    else /* if (ret_code == 3) */ {
        mg_printf_http_chunk(rpc->nc,
                "{\"result\": %u}",
                (unsigned)arg);
    }
    mg_send_http_chunk(rpc->nc, "", 0); /* Send empty chunk, the end of response */
}

// {"cmd":"sample_rate","val":1024000}
// http --stream --timeout=70 :8433/events
//s.a. https://developer.twitter.com/en/docs/tutorials/consuming-streaming-data.html
static void handle_json_events(struct mg_connection *nc, struct http_message *hm)
{
    UNUSED(hm);
    /* Send headers */
    mg_printf(nc, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");

    /* Mark connection */
    struct nc_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        WARN_CALLOC("handle_json_events()");
        return;
    }
    ctx->is_chunked = 1;
    nc->user_data   = ctx;

    mg_set_timer(nc, mg_time() + KEEP_ALIVE); // set keep alive timer
}

// (echo "GET /stream HTTP/1.0\n"; sleep 600) | socat - tcp:127.0.0.1:8433
static void handle_json_stream(struct mg_connection *nc, struct http_message *hm)
{
    UNUSED(hm);
    /* Send headers */
    mg_printf(nc, "HTTP/1.1 200 OK\r\n\r\n");

    /* Mark connection */
    struct nc_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        WARN_CALLOC("handle_json_stream()");
        return;
    }
    ctx->is_chunked = 0;
    nc->user_data   = ctx;

    mg_set_timer(nc, mg_time() + KEEP_ALIVE); // set keep alive timer
}

// Handles GET with query string and POST with form-encoded body
// curl -D - 'http://127.0.0.1:8433/cmd?cmd=report_meta&arg=level'
// curl -D - -d "cmd=report_meta&arg=level" -X POST 'http://127.0.0.1:8433/cmd'
// http :8433/cmd cmd==center_frequency val==868000000'
// http --form POST :8433/cmd cmd=report_meta arg=level val=1
static void handle_cmd_rpc(struct mg_connection *nc, struct http_message *hm)
{
    struct http_server_context *ctx = nc->user_data;
    char cmd[100], arg[100], val[100];
    rpc_t rpc = {
            .nc = nc,
            .response = rpc_response_jsoncmd,
            .method = cmd,
            .arg = arg,
    };

    /* Send headers */
    mg_printf(nc, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");

    /* Get URL variables */
    if (mg_vcmp(&hm->method, "GET") == 0) {
        mg_get_http_var(&hm->query_string, "cmd", cmd, sizeof(cmd));
        mg_get_http_var(&hm->query_string, "arg", arg, sizeof(arg));
        mg_get_http_var(&hm->query_string, "val", val, sizeof(val));
    }
    /* Get form variables */
    else {
        mg_get_http_var(&hm->body, "cmd", cmd, sizeof(cmd));
        mg_get_http_var(&hm->body, "arg", arg, sizeof(arg));
        mg_get_http_var(&hm->body, "val", val, sizeof(val));
    }
    char *endptr = NULL;
    rpc.val = strtol(val, &endptr, 10);
    fprintf(stderr, "POST Got %s, arg %s, val %s (%u)\n", cmd, arg, val, rpc.val);

    rpc_exec(&rpc, ctx->cfg);
}

// Handles POST with JSONRPC command
// http POST :8433/jsonrpc jsonrpc=2.0 method=sample_rate params:='[1024000]'
static void handle_json_rpc(struct mg_connection *nc, struct http_message *hm)
{
    struct http_server_context *ctx = nc->user_data;

    rpc_t rpc = {
            .nc       = nc,
            .response = rpc_response_jsonrpc,
    };

    /* Send headers */
    mg_printf(nc, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");

    /* Parse JSON */
    int ret = jsonrpc_parse(&rpc, &hm->body);
    if (!ret) {
        rpc_exec(&rpc, ctx->cfg);
    }
    else {
        char *error = "{\"error\":\"Invalid command\"}";
        mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, error, strlen(error));
    }

    free(rpc.method);
    free(rpc.id);
    free(rpc.arg);
}

// Handles WS with JSON command
static void handle_ws_rpc(struct mg_connection *nc, struct websocket_message *wm)
{
    struct http_server_context *ctx = nc->user_data;

    rpc_t rpc = {
            .nc       = nc,
            .response = rpc_response_ws,
    };

    struct mg_str d = {(char *)wm->data, wm->size};

    /* Parse JSON */
    int ret = json_parse(&rpc, &d);
    if (!ret) {
        rpc_exec(&rpc, ctx->cfg);
    }
    else {
        char *error = "{\"error\":\"Invalid command\"}";
        mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, error, strlen(error));
    }

    free(rpc.method);
    free(rpc.id);
    free(rpc.arg);
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data);

static void send_keep_alive(struct mg_connection *nc)
{
    if (nc->handler != ev_handler)
        return; // this should not happen

    struct nc_context *ctx = nc->user_data;
    if (!ctx)
        return; // this should not happen

    if (ctx->is_chunked) {
        mg_send_http_chunk(nc, "\r\n", 2);
    }
    else {
        mg_send(nc, "\r\n", 2);
    }
    mg_set_timer(nc, mg_time() + KEEP_ALIVE); // reset keep alive timer
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data)
{
    switch (ev) {
    case MG_EV_TIMER:
        send_keep_alive(nc);
        break;
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
        struct http_server_context *ctx = nc->user_data;
        /* New websocket connection. Send meta. */
        data_t *meta = meta_data(ctx->cfg);
        data_output_print(ctx->output, meta);
        data_free(meta);
        /* Send history */
        for (void **iter = ring_list_iter(ctx->history); iter; iter = ring_list_next(ctx->history, iter))
            mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, (char *)*iter, strlen((char *)*iter));
        break;
    }
    case MG_EV_WEBSOCKET_FRAME: {
        struct websocket_message *wm = (struct websocket_message *)ev_data;

        handle_ws_rpc(nc, wm);
        break;
    }
    case MG_EV_HTTP_REQUEST: {
        struct http_message *hm = (struct http_message *)ev_data;

        if (mg_vcmp(&hm->method, "OPTIONS") == 0) {
            handle_options(nc, hm);
        }
        else if (mg_vcmp(&hm->uri, "/") == 0) {
            handle_get(nc, hm, INDEX_HTML, sizeof(INDEX_HTML));
            handle_redirect(nc, hm);
        }
        else if (mg_vcmp(&hm->uri, "/ui") == 0) {
            handle_redirect(nc, hm);
        }
        else if (mg_vcmp(&hm->uri, "/jsonrpc") == 0) {
            handle_json_rpc(nc, hm);
        }
        else if (mg_vcmp(&hm->uri, "/cmd") == 0) {
            handle_cmd_rpc(nc, hm);
        }
        else if (mg_vcmp(&hm->uri, "/events") == 0) {
            handle_json_events(nc, hm);
        }
        else if (mg_vcmp(&hm->uri, "/stream") == 0) {
            handle_json_stream(nc, hm);
        }
        else if (mg_vcmp(&hm->uri, "/api") == 0) {
            //handle_api_query(nc, hm);
        }
#ifdef SERVE_STATIC
        else {
            struct http_server_context *ctx = nc->user_data;
            mg_serve_http(nc, hm, ctx->server_opts); /* Serve static content */
        }
#endif
        break;
    }
    case MG_EV_CLOSE:
        //fprintf(stderr, "MG_EV_CLOSE %p %p %p\n", ev_data, nc, nc->user_data);
        break;
    default:
        break;
    }
}

static int is_websocket(const struct mg_connection *nc)
{
    return nc->flags & MG_F_IS_WEBSOCKET;
}

// event handler to broadcast to all our sockets
static void http_broadcast_send(struct http_server_context *ctx, char const *msg, size_t len)
{
    struct mg_connection *nc;
    struct mg_mgr *mgr = ctx->conn->mgr;

    char *dup = strdup(msg);
    if (!dup) {
        WARN_STRDUP("http_broadcast_send()");
    }
    else {
        free(ring_list_push(ctx->history, dup));
    }

    for (nc = mg_next(mgr, NULL); nc != NULL; nc = mg_next(mgr, nc)) {
        if (nc->handler != ev_handler)
            continue;

        struct nc_context *cctx = nc->user_data; // might not be valid
        if (is_websocket(nc)) {
            mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, msg, len);
        }
        else if (cctx && cctx->is_chunked) {
            mg_send_http_chunk(nc, msg, len);
            mg_send_http_chunk(nc, "\r\n", 2);
            mg_set_timer(nc, mg_time() + KEEP_ALIVE); // reset keep alive timer
        }
        else if (cctx && !cctx->is_chunked) {
            mg_send(nc, msg, len);
            mg_send(nc, "\r\n", 2);
            mg_set_timer(nc, mg_time() + KEEP_ALIVE); // reset keep alive timer
        }
    }
}

static struct http_server_context *http_server_start(struct mg_mgr *mgr, char const *host, char const *port, r_cfg_t *cfg, struct data_output *output)
{
    struct mg_bind_opts bind_opts;
    const char *err_str;

    //struct http_server_context
    struct http_server_context *ctx = calloc(1, sizeof(struct http_server_context));
    if (!ctx) {
        WARN_CALLOC("http_server_start()");
        return NULL;
    }

    ctx->cfg     = cfg;
    ctx->output  = output;
    ctx->history = ring_list_new(DEFAULT_HISTORY_SIZE);

    char address[253 + 6 + 1]; // dns max + port
    // if the host is an IPv6 address it needs quoting
    if (strchr(host, ':'))
        snprintf(address, sizeof(address), "[%s]:%s", host, port);
    else
        snprintf(address, sizeof(address), "%s:%s", host, port);

    /* Set HTTP server options */
    memset(&bind_opts, 0, sizeof(bind_opts));
    bind_opts.user_data = ctx;
    bind_opts.error_string = &err_str;

    ctx->conn = mg_bind_opt(mgr, address, ev_handler, bind_opts);
    if (ctx->conn == NULL) {
        print_logf(LOG_ERROR, __func__, "Error starting server on address %s: %s", address,
                *bind_opts.error_string);
        free(ctx);
        return NULL;
    }

    mg_set_protocol_http_websocket(ctx->conn);
    ctx->server_opts.document_root            = "."; // Serve current directory
    ctx->server_opts.enable_directory_listing = "yes";

    print_logf(LOG_NOTICE, "HTTP server", "Serving HTTP-API on address %s, serving %s", address,
            ctx->server_opts.document_root);

    return ctx;
}

#define SHUTDOWN_JSON "{\"shutdown\":\"goodbye\"}"

static int http_server_stop(struct http_server_context *ctx)
{
    if (!ctx)
        return 0;

    // close the server
    ctx->conn->user_data = NULL;
    ctx->conn->flags |= MG_F_CLOSE_IMMEDIATELY;

    // close connections with a goodbye
    struct mg_mgr *mgr = ctx->conn->mgr;
    for (struct mg_connection *nc = mg_next(mgr, NULL); nc != NULL; nc = mg_next(mgr, nc)) {
        if (nc->handler != ev_handler)
            continue;

        struct nc_context *cctx = nc->user_data; // might not be valid
        if (is_websocket(nc)) {
            mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, SHUTDOWN_JSON, sizeof(SHUTDOWN_JSON) - 1);
        }
        else if (cctx && cctx->is_chunked) {
            mg_send_http_chunk(nc, SHUTDOWN_JSON, sizeof(SHUTDOWN_JSON) - 1);
            mg_send_http_chunk(nc, "\r\n", 2);
            mg_send_http_chunk(nc, "", 0);            /* Send empty chunk, the end of response */
        }
        else if (cctx && !cctx->is_chunked) {
            mg_send(nc, SHUTDOWN_JSON, sizeof(SHUTDOWN_JSON) - 1);
            mg_send(nc, "\r\n", 2);
        }
    }

    for (void **iter = ring_list_iter(ctx->history); iter; iter = ring_list_next(ctx->history, iter))
        free((data_t *)*iter);
    ring_list_free(ctx->history);

    free(ctx);

    return 0;
}

/* HTTP data output */

typedef struct {
    struct data_output output;
    struct http_server_context *server;
} data_output_http_t;

static void R_API_CALLCONV print_http_data(data_output_t *output, data_t *data, char const *format)
{
    UNUSED(format);
    data_output_http_t *http = (data_output_http_t *)output;

    // collect well-known top level keys
    data_t *data_model = NULL;
    for (data_t *d = data; d; d = d->next) {
        if (!strcmp(d->key, "model"))
            data_model = d;
    }

    if (data_model) {
        // "events"
        char buf[2048]; // we expect the biggest strings to be around 500 bytes.
        size_t len = data_print_jsons(data, buf, sizeof(buf));
        http_broadcast_send(http->server, buf, len);
    }
    else {
        // "states"
        size_t buf_size = 20000; // state message need a large buffer
        char *buf       = malloc(buf_size);
        if (!buf) {
            WARN_MALLOC("print_http_data()");
            return; // NOTE: skip output on alloc failure.
        }
        size_t len = data_print_jsons(data, buf, buf_size);
        http_broadcast_send(http->server, buf, len);
        free(buf);
    }
}

static void R_API_CALLCONV data_output_http_free(data_output_t *output)
{
    data_output_http_t *http = (data_output_http_t *)output;

    if (!http)
        return;

    http_server_stop(http->server);

    free(http);
}

struct data_output *data_output_http_create(struct mg_mgr *mgr, char const *host, char const *port, r_cfg_t *cfg)
{
    data_output_http_t *http = calloc(1, sizeof(data_output_http_t));
    if (!http) {
        WARN_CALLOC("data_output_http_create()");
        return NULL;
    }

    http->output.log_level    = LOG_TRACE; // sensible default, not parsed from args
    http->output.print_data   = print_http_data;
    http->output.output_free  = data_output_http_free;

    http->server = http_server_start(mgr, host, port, cfg, &http->output);
    if (!http->server) {
        exit(1);
    }

    return &http->output;
}
