/** @file
    MQTT output for rtl_433 events

    Copyright (C) 2019 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

// note: our unit header includes unistd.h for gethostname() via data.h
#include "output_mqtt.h"
#include "optparse.h"
#include "bit_util.h"
#include "logger.h"
#include "fatal.h"
#include "r_util.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mongoose.h"

/* MQTT client abstraction */

typedef struct mqtt_client {
    struct mg_connect_opts connect_opts;
    struct mg_send_mqtt_handshake_opts mqtt_opts;
    struct mg_connection *conn;
    struct mg_connection *timer;
    int reconnect_delay;
    int prev_status;
    char address[253 + 6 + 1]; // dns max + port
    char client_id[256];
    uint16_t message_id;
    int publish_flags; // MG_MQTT_RETAIN | MG_MQTT_QOS(0)
} mqtt_client_t;

static void mqtt_client_event(struct mg_connection *nc, int ev, void *ev_data)
{
    // note that while shutting down the ctx is NULL
    mqtt_client_t *ctx = (mqtt_client_t *)nc->user_data;
    // only valid in MG_EV_MQTT_ events
    struct mg_mqtt_message *msg = (struct mg_mqtt_message *)ev_data;

    //if (ev != MG_EV_POLL)
    //    fprintf(stderr, "MQTT user handler got event %d\n", ev);

    switch (ev) {
    case MG_EV_CONNECT: {
        int connect_status = *(int *)ev_data;
        if (connect_status == 0) {
            // Success
            print_log(LOG_NOTICE, "MQTT", "MQTT Connected...");
            mg_set_protocol_mqtt(nc);
            if (ctx) {
                ctx->reconnect_delay = 0;
                mg_send_mqtt_handshake_opt(nc, ctx->client_id, ctx->mqtt_opts);
            }
        }
        else {
            // Error, print only once
            if (ctx && ctx->prev_status != connect_status) {
                print_logf(LOG_WARNING, "MQTT", "MQTT connect error: %s", strerror(connect_status));
            }
        }
        if (ctx) {
            ctx->prev_status = connect_status;
        }
        break;
    }
    case MG_EV_MQTT_CONNACK:
        if (msg->connack_ret_code != MG_EV_MQTT_CONNACK_ACCEPTED) {
            print_logf(LOG_WARNING, "MQTT", "MQTT Connection error: %u", msg->connack_ret_code);
        }
        else {
            print_log(LOG_NOTICE, "MQTT", "MQTT Connection established.");
        }
        break;
    case MG_EV_MQTT_PUBACK:
        print_logf(LOG_NOTICE, "MQTT", "MQTT Message publishing acknowledged (msg_id: %u)", msg->message_id);
        break;
    case MG_EV_MQTT_SUBACK:
        print_log(LOG_NOTICE, "MQTT", "MQTT Subscription acknowledged.");
        break;
    case MG_EV_MQTT_PUBLISH: {
        print_logf(LOG_NOTICE, "MQTT", "MQTT Incoming message %.*s: %.*s", (int)msg->topic.len,
                msg->topic.p, (int)msg->payload.len, msg->payload.p);
        break;
    }
    case MG_EV_CLOSE:
        if (!ctx) {
            break; // shutting down
        }
        ctx->conn = NULL;
        if (!ctx->timer) {
            break; // shutting down
        }
        if (ctx->prev_status == 0) {
            print_log(LOG_WARNING, "MQTT", "MQTT Connection lost, reconnecting...");
        }
        // Timer for reconnect attempt, sends us MG_EV_TIMER event
        mg_set_timer(ctx->timer, mg_time() + ctx->reconnect_delay);
        if (ctx->reconnect_delay < 60) {
            // 0, 1, 3, 6, 10, 16, 25, 39, 60
            ctx->reconnect_delay = (ctx->reconnect_delay + 1) * 3 / 2;
        }
        break;
    }
}

static void mqtt_client_timer(struct mg_connection *nc, int ev, void *ev_data)
{
    // note that while shutting down the ctx is NULL
    mqtt_client_t *ctx = (mqtt_client_t *)nc->user_data;
    (void)ev_data;

    //if (ev != MG_EV_POLL)
    //    fprintf(stderr, "MQTT timer handler got event %d\n", ev);

    switch (ev) {
    case MG_EV_TIMER: {
        // Try to reconnect
        char const *error_string = NULL;
        ctx->connect_opts.error_string = &error_string;
        ctx->conn = mg_connect_opt(nc->mgr, ctx->address, mqtt_client_event, ctx->connect_opts);
        ctx->connect_opts.error_string = NULL;
        if (!ctx->conn) {
            print_logf(LOG_WARNING, "MQTT", "MQTT connect (%s) failed%s%s", ctx->address,
                    error_string ? ": " : "", error_string ? error_string : "");
        }
        break;
    }
    }
}

static mqtt_client_t *mqtt_client_init(struct mg_mgr *mgr, tls_opts_t *tls_opts, char const *host, char const *port, char const *user, char const *pass, char const *client_id, int retain, int qos)
{
    mqtt_client_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        FATAL_CALLOC("mqtt_client_init()");

    ctx->mqtt_opts.user_name = user;
    ctx->mqtt_opts.password  = pass;
    ctx->publish_flags  = MG_MQTT_QOS(qos) | (retain ? MG_MQTT_RETAIN : 0);
    // TODO: these should be user configurable options
    //ctx->opts.keepalive = 60;
    //ctx->timeout = 10000L;
    //ctx->cleansession = 1;
    snprintf(ctx->client_id, sizeof(ctx->client_id), "%s", client_id);

    // if the host is an IPv6 address it needs quoting
    if (strchr(host, ':'))
        snprintf(ctx->address, sizeof(ctx->address), "[%s]:%s", host, port);
    else
        snprintf(ctx->address, sizeof(ctx->address), "%s:%s", host, port);

    ctx->connect_opts.user_data = ctx;
    if (tls_opts && tls_opts->tls_ca_cert) {
        print_logf(LOG_INFO, "MQTT", "mqtts (TLS) parameters are: "
                                       "tls_cert=%s "
                                       "tls_key=%s "
                                       "tls_ca_cert=%s "
                                       "tls_cipher_suites=%s "
                                       "tls_server_name=%s "
                                       "tls_psk_identity=%s "
                                       "tls_psk_key=%s ",
                tls_opts->tls_cert,
                tls_opts->tls_key,
                tls_opts->tls_ca_cert,
                tls_opts->tls_cipher_suites,
                tls_opts->tls_server_name,
                tls_opts->tls_psk_identity,
                tls_opts->tls_psk_key);
#if MG_ENABLE_SSL
        ctx->connect_opts.ssl_cert          = tls_opts->tls_cert;
        ctx->connect_opts.ssl_key           = tls_opts->tls_key;
        ctx->connect_opts.ssl_ca_cert       = tls_opts->tls_ca_cert;
        ctx->connect_opts.ssl_cipher_suites = tls_opts->tls_cipher_suites;
        ctx->connect_opts.ssl_server_name   = tls_opts->tls_server_name;
        ctx->connect_opts.ssl_psk_identity  = tls_opts->tls_psk_identity;
        ctx->connect_opts.ssl_psk_key       = tls_opts->tls_psk_key;
#else
        print_log(LOG_FATAL, __func__, "mqtts (TLS) not available");
        exit(1);
#endif
    }

    // add dummy socket to receive timer events
    struct mg_add_sock_opts opts = {.user_data = ctx};
    ctx->timer = mg_add_sock_opt(mgr, INVALID_SOCKET, mqtt_client_timer, opts);

    char const *error_string = NULL;
    ctx->connect_opts.error_string = &error_string;
    ctx->conn = mg_connect_opt(mgr, ctx->address, mqtt_client_event, ctx->connect_opts);
    ctx->connect_opts.error_string = NULL;
    if (!ctx->conn) {
        print_logf(LOG_FATAL, "MQTT", "MQTT connect (%s) failed%s%s", ctx->address,
                error_string ? ": " : "", error_string ? error_string : "");
        exit(1);
    }

    return ctx;
}

static void mqtt_client_publish(mqtt_client_t *ctx, char const *topic, char const *str)
{
    if (!ctx->conn || !ctx->conn->proto_handler)
        return;

    ctx->message_id++;
    mg_mqtt_publish(ctx->conn, topic, ctx->message_id, ctx->publish_flags, str, strlen(str));
}

static void mqtt_client_free(mqtt_client_t *ctx)
{
    if (ctx && ctx->conn) {
        ctx->conn->user_data = NULL;
        ctx->conn->flags |= MG_F_CLOSE_IMMEDIATELY;
    }
    free(ctx);
}

/* Helper */

/// clean the topic inplace to [-.A-Za-z0-9], esp. not whitespace, +, #, /, $
static char *mqtt_sanitize_topic(char *topic)
{
    for (char *p = topic; *p; ++p)
        if (*p != '-' && *p != '.' && (*p < 'A' || *p > 'Z') && (*p < 'a' || *p > 'z') && (*p < '0' || *p > '9'))
            *p = '_';

    return topic;
}

/* MQTT printer */

typedef struct {
    struct data_output output;
    mqtt_client_t *mqc;
    char topic[256];
    char hostname[64];
    char *devices;
    char *events;
    char *states;
    //char *homie;
    //char *hass;
} data_output_mqtt_t;

static void R_API_CALLCONV print_mqtt_array(data_output_t *output, data_array_t *array, char const *format)
{
    data_output_mqtt_t *mqtt = (data_output_mqtt_t *)output;

    char *orig = mqtt->topic + strlen(mqtt->topic); // save current topic

    for (int c = 0; c < array->num_values; ++c) {
        sprintf(orig, "/%d", c);
        print_array_value(output, array, format, c);
    }
    *orig = '\0'; // restore topic
}

static char *append_topic(char *topic, data_t *data)
{
    if (data->type == DATA_STRING) {
        strcpy(topic, data->value.v_ptr); // NOLINT
        mqtt_sanitize_topic(topic);
        topic += strlen(data->value.v_ptr);
    }
    else if (data->type == DATA_INT) {
        topic += sprintf(topic, "%d", data->value.v_int);
    }
    else {
        print_logf(LOG_ERROR, __func__, "Can't append data type %d to topic", data->type);
    }

    return topic;
}

static char *expand_topic(char *topic, char const *format, data_t *data, char const *hostname)
{
    // collect well-known top level keys
    data_t *data_type    = NULL;
    data_t *data_model   = NULL;
    data_t *data_subtype = NULL;
    data_t *data_channel = NULL;
    data_t *data_id      = NULL;
    data_t *data_protocol = NULL;
    for (data_t *d = data; d; d = d->next) {
        if (!strcmp(d->key, "type"))
            data_type = d;
        else if (!strcmp(d->key, "model"))
            data_model = d;
        else if (!strcmp(d->key, "subtype"))
            data_subtype = d;
        else if (!strcmp(d->key, "channel"))
            data_channel = d;
        else if (!strcmp(d->key, "id"))
            data_id = d;
        else if (!strcmp(d->key, "protocol")) // NOTE: needs "-M protocol"
            data_protocol = d;
    }

    // consume entire format string
    while (format && *format) {
        data_t *data_token  = NULL;
        char const *string_token = NULL;
        int leading_slash   = 0;
        char const *t_start = NULL;
        char const *t_end   = NULL;
        char const *d_start = NULL;
        char const *d_end   = NULL;
        // copy until '['
        while (*format && *format != '[')
            *topic++ = *format++;
        // skip '['
        if (!*format)
            break;
        ++format;
        // read slash
        if (!leading_slash && (*format < 'a' || *format > 'z')) {
            leading_slash = *format;
            format++;
        }
        // read key until : or ]
        t_start = t_end = format;
        while (*format && *format != ':' && *format != ']' && *format != '[')
            t_end = ++format;
        // read default until ]
        if (*format == ':') {
            d_start = d_end = ++format;
            while (*format && *format != ']' && *format != '[')
                d_end = ++format;
        }
        // check for proper closing
        if (*format != ']') {
            print_log(LOG_FATAL, __func__, "unterminated token");
            exit(1);
        }
        ++format;

        // resolve token
        if (!strncmp(t_start, "hostname", t_end - t_start))
            string_token = hostname;
        else if (!strncmp(t_start, "type", t_end - t_start))
            data_token = data_type;
        else if (!strncmp(t_start, "model", t_end - t_start))
            data_token = data_model;
        else if (!strncmp(t_start, "subtype", t_end - t_start))
            data_token = data_subtype;
        else if (!strncmp(t_start, "channel", t_end - t_start))
            data_token = data_channel;
        else if (!strncmp(t_start, "id", t_end - t_start))
            data_token = data_id;
        else if (!strncmp(t_start, "protocol", t_end - t_start))
            data_token = data_protocol;
        else {
            print_logf(LOG_FATAL, __func__, "unknown token \"%.*s\"", (int)(t_end - t_start), t_start);
            exit(1);
        }

        // append token or default
        if (!data_token && !string_token && !d_start)
            continue;
        if (leading_slash)
            *topic++ = leading_slash;
        if (data_token)
            topic = append_topic(topic, data_token);
        else if (string_token)
            topic += sprintf(topic, "%s", string_token);
        else
            topic += sprintf(topic, "%.*s", (int)(d_end - d_start), d_start);
    }

    *topic = '\0';
    return topic;
}

// <prefix>[/type][/model][/subtype][/channel][/id]/battery: "OK"|"LOW"
static void R_API_CALLCONV print_mqtt_data(data_output_t *output, data_t *data, char const *format)
{
    UNUSED(format);
    data_output_mqtt_t *mqtt = (data_output_mqtt_t *)output;

    char *orig = mqtt->topic + strlen(mqtt->topic); // save current topic
    char *end  = orig;

    // top-level only
    if (!*mqtt->topic) {
        // collect well-known top level keys
        data_t *data_model = NULL;
        for (data_t *d = data; d; d = d->next) {
            if (!strcmp(d->key, "model"))
                data_model = d;
        }

        // "states" topic
        if (!data_model) {
            if (mqtt->states) {
                size_t message_size = 20000; // state message need a large buffer
                char *message       = malloc(message_size);
                if (!message) {
                    WARN_MALLOC("print_mqtt_data()");
                    return; // NOTE: skip output on alloc failure.
                }
                data_print_jsons(data, message, message_size);
                expand_topic(mqtt->topic, mqtt->states, data, mqtt->hostname);
                mqtt_client_publish(mqtt->mqc, mqtt->topic, message);
                *mqtt->topic = '\0'; // clear topic
                free(message);
            }
            return;
        }

        // "events" topic
        if (mqtt->events) {
            char message[2048]; // we expect the biggest strings to be around 500 bytes.
            data_print_jsons(data, message, sizeof(message));
            expand_topic(mqtt->topic, mqtt->events, data, mqtt->hostname);
            mqtt_client_publish(mqtt->mqc, mqtt->topic, message);
            *mqtt->topic = '\0'; // clear topic
        }

        // "devices" topic
        if (!mqtt->devices) {
            return;
        }

        end = expand_topic(mqtt->topic, mqtt->devices, data, mqtt->hostname);
    }

    while (data) {
        if (!strcmp(data->key, "type")
                || !strcmp(data->key, "model")
                || !strcmp(data->key, "subtype")) {
            // skip, except "id", "channel"
        }
        else {
            // push topic
            *end = '/';
            strcpy(end + 1, data->key); // NOLINT
            print_value(output, data->type, data->value, data->format);
            *end = '\0'; // pop topic
        }
        data = data->next;
    }
    *orig = '\0'; // restore topic
}

static void R_API_CALLCONV print_mqtt_string(data_output_t *output, char const *str, char const *format)
{
    UNUSED(format);
    data_output_mqtt_t *mqtt = (data_output_mqtt_t *)output;
    mqtt_client_publish(mqtt->mqc, mqtt->topic, str);
}

static void R_API_CALLCONV print_mqtt_double(data_output_t *output, double data, char const *format)
{
    char str[20];
    // use scientific notation for very big/small values
    if (data > 1e7 || data < 1e-4) {
        snprintf(str, sizeof(str), "%g", data);
    }
    else {
        int ret = snprintf(str, sizeof(str), "%.5f", data);
        // remove trailing zeros, always keep one digit after the decimal point
        char *p = str + ret - 1;
        while (*p == '0' && p[-1] != '.') {
            *p-- = '\0';
        }
    }

    print_mqtt_string(output, str, format);
}

static void R_API_CALLCONV print_mqtt_int(data_output_t *output, int data, char const *format)
{
    char str[20];
    snprintf(str, sizeof(str), "%d", data);
    print_mqtt_string(output, str, format);
}

static void R_API_CALLCONV data_output_mqtt_free(data_output_t *output)
{
    data_output_mqtt_t *mqtt = (data_output_mqtt_t *)output;

    if (!mqtt)
        return;

    free(mqtt->devices);
    free(mqtt->events);
    free(mqtt->states);
    //free(mqtt->homie);
    //free(mqtt->hass);

    mqtt_client_free(mqtt->mqc);

    free(mqtt);
}

static char *mqtt_topic_default(char const *topic, char const *base, char const *suffix)
{
    char path[256];
    char const *p;
    if (topic) {
        p = topic;
    }
    else if (!base) {
        p = suffix;
    }
    else {
        snprintf(path, sizeof(path), "%s/%s", base, suffix);
        p = path;
    }

    char *ret = strdup(p);
    if (!ret)
        WARN_STRDUP("mqtt_topic_default()");
    return ret;
}

struct data_output *data_output_mqtt_create(struct mg_mgr *mgr, char *param, char const *dev_hint)
{
    data_output_mqtt_t *mqtt = calloc(1, sizeof(data_output_mqtt_t));
    if (!mqtt)
        FATAL_CALLOC("data_output_mqtt_create()");

    gethostname(mqtt->hostname, sizeof(mqtt->hostname) - 1);
    mqtt->hostname[sizeof(mqtt->hostname) - 1] = '\0';
    // only use hostname, not domain part
    char *dot = strchr(mqtt->hostname, '.');
    if (dot)
        *dot = '\0';
    //fprintf(stderr, "Hostname: %s\n", hostname);

    // generate a short deterministic client_id to identify this input device on restart
    uint16_t host_crc = crc16((uint8_t *)mqtt->hostname, strlen(mqtt->hostname), 0x1021, 0xffff);
    uint16_t devq_crc = crc16((uint8_t *)dev_hint, dev_hint ? strlen(dev_hint) : 0, 0x1021, 0xffff);
    uint16_t parm_crc = crc16((uint8_t *)param, param ? strlen(param) : 0, 0x1021, 0xffff);
    char client_id[21];
    /// MQTT 3.1.1 specifies that the broker MUST accept clients id's between 1 and 23 characters
    snprintf(client_id, sizeof(client_id), "rtl_433-%04x%04x%04x", host_crc, devq_crc, parm_crc);

    // default base topic
    char default_base_topic[8 + sizeof(mqtt->hostname)];
    snprintf(default_base_topic, sizeof(default_base_topic), "rtl_433/%s", mqtt->hostname);
    char const *base_topic = default_base_topic;

    // default topics
    char const *path_devices = "devices[/type][/model][/subtype][/channel][/id]";
    char const *path_events = "events";
    char const *path_states = "states";

    // get user and pass from env vars if available.
    char *user = getenv("MQTT_USERNAME");
    char *pass = getenv("MQTT_PASSWORD");
    int retain = 0;
    int qos = 0;

    // parse host and port
    tls_opts_t tls_opts = {0};
    if (param && strncmp(param, "mqtts", 5) == 0) {
        tls_opts.tls_ca_cert = "*"; // TLS is enabled but no cert verification is performed.
    }
    param      = arg_param(param); // strip scheme
    char const *host = "localhost";
    char const *port = tls_opts.tls_ca_cert ? "8883" : "1883";
    char *opts = hostport_param(param, &host, &port);
    print_logf(LOG_CRITICAL, "MQTT", "Publishing MQTT data to %s port %s%s", host, port, tls_opts.tls_ca_cert ? " (TLS)" : "");

    // parse auth and format options
    char *key, *val;
    while (getkwargs(&opts, &key, &val)) {
        key = remove_ws(key);
        val = trim_ws(val);
        if (!key || !*key)
            continue;
        else if (!strcasecmp(key, "u") || !strcasecmp(key, "user"))
            user = val;
        else if (!strcasecmp(key, "p") || !strcasecmp(key, "pass"))
            pass = val;
        else if (!strcasecmp(key, "r") || !strcasecmp(key, "retain"))
            retain = atobv(val, 1);
        else if (!strcasecmp(key, "q") || !strcasecmp(key, "qos"))
            qos = atoiv(val, 1);
        else if (!strcasecmp(key, "b") || !strcasecmp(key, "base"))
            base_topic = val;
        // Simple key-topic mapping
        else if (!strcasecmp(key, "d") || !strcasecmp(key, "devices"))
            mqtt->devices = mqtt_topic_default(val, base_topic, path_devices);
        // deprecated, remove this
        else if (!strcasecmp(key, "c") || !strcasecmp(key, "usechannel")) {
            print_log(LOG_FATAL, "MQTT", "\"usechannel=...\" has been removed. Use a topic format string:");
            print_log(LOG_FATAL, "MQTT", "for \"afterid\"   use e.g. \"devices=rtl_433/[hostname]/devices[/type][/model][/subtype][/id][/channel]\"");
            print_log(LOG_FATAL, "MQTT", "for \"beforeid\"  use e.g. \"devices=rtl_433/[hostname]/devices[/type][/model][/subtype][/channel][/id]\"");
            print_log(LOG_FATAL, "MQTT", "for \"replaceid\" use e.g. \"devices=rtl_433/[hostname]/devices[/type][/model][/subtype][/channel]\"");
            print_log(LOG_FATAL, "MQTT", "for \"no\"        use e.g. \"devices=rtl_433/[hostname]/devices[/type][/model][/subtype][/id]\"");
            exit(1);
        }
        // JSON events to single topic
        else if (!strcasecmp(key, "e") || !strcasecmp(key, "events"))
            mqtt->events = mqtt_topic_default(val, base_topic, path_events);
        // JSON states to single topic
        else if (!strcasecmp(key, "s") || !strcasecmp(key, "states"))
            mqtt->states = mqtt_topic_default(val, base_topic, path_states);
        // TODO: Homie Convention https://homieiot.github.io/
        //else if (!strcasecmp(key, "o") || !strcasecmp(key, "homie"))
        //    mqtt->homie = mqtt_topic_default(val, NULL, "homie"); // base topic
        // TODO: Home Assistant MQTT discovery https://www.home-assistant.io/docs/mqtt/discovery/
        //else if (!strcasecmp(key, "a") || !strcasecmp(key, "hass"))
        //    mqtt->hass = mqtt_topic_default(val, NULL, "homeassistant"); // discovery prefix
        else if (!tls_param(&tls_opts, key, val)) {
            // ok
        }
        else {
            print_logf(LOG_FATAL, __func__, "Invalid key \"%s\" option.", key);
            exit(1);
        }
    }

    // Default is to use all formats
    if (!mqtt->devices && !mqtt->events && !mqtt->states) {
        mqtt->devices = mqtt_topic_default(NULL, base_topic, path_devices);
        mqtt->events  = mqtt_topic_default(NULL, base_topic, path_events);
        mqtt->states  = mqtt_topic_default(NULL, base_topic, path_states);
    }
    if (mqtt->devices)
        print_logf(LOG_NOTICE, "MQTT", "Publishing device info to MQTT topic \"%s\".", mqtt->devices);
    if (mqtt->events)
        print_logf(LOG_NOTICE, "MQTT", "Publishing events info to MQTT topic \"%s\".", mqtt->events);
    if (mqtt->states)
        print_logf(LOG_NOTICE, "MQTT", "Publishing states info to MQTT topic \"%s\".", mqtt->states);

    mqtt->output.print_data   = print_mqtt_data;
    mqtt->output.print_array  = print_mqtt_array;
    mqtt->output.print_string = print_mqtt_string;
    mqtt->output.print_double = print_mqtt_double;
    mqtt->output.print_int    = print_mqtt_int;
    mqtt->output.output_free  = data_output_mqtt_free;

    mqtt->mqc = mqtt_client_init(mgr, &tls_opts, host, port, user, pass, client_id, retain, qos);

    return (struct data_output *)mqtt;
}
