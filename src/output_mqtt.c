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
#include "util.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mongoose.h"

/* MQTT client abstraction */

typedef struct mqtt_client {
    struct mg_send_mqtt_handshake_opts opts;
    int prev_status;
    char address[253 + 6 + 1]; // dns max + port
    char client_id[256];
    uint16_t message_id;
    int publish_flags; // MG_MQTT_RETAIN | MG_MQTT_QOS(0)
} mqtt_client_t;

static void mqtt_client_event(struct mg_connection *nc, int ev, void *ev_data)
{
    // note that while shutting down the ctx is NULL
    mqtt_client_t *ctx = (mqtt_client_t *)nc->mgr->user_data;
    // only valid in MG_EV_MQTT_ events
    struct mg_mqtt_message *msg = (struct mg_mqtt_message *)ev_data;

    //if (ev != MG_EV_POLL)
    //    fprintf(stderr, "MQTT user handler got event %d\n", ev);

    switch (ev) {
    case MG_EV_CONNECT: {
        int connect_status = *(int *)ev_data;
        if (connect_status == 0) {
            // Success
            fprintf(stderr, "MQTT Connected...\n");
            mg_set_protocol_mqtt(nc);
            if (ctx)
                mg_send_mqtt_handshake_opt(nc, ctx->client_id, ctx->opts);
        }
        else {
            // Error, print only once
            if (ctx && ctx->prev_status != connect_status)
                fprintf(stderr, "MQTT connect error: %s\n", strerror(connect_status));
        }
        if (ctx)
            ctx->prev_status = connect_status;
        break;
    }
    case MG_EV_MQTT_CONNACK:
        if (msg->connack_ret_code != MG_EV_MQTT_CONNACK_ACCEPTED) {
            fprintf(stderr, "MQTT Connection error: %d\n", msg->connack_ret_code);
        }
        else {
            fprintf(stderr, "MQTT Connection established.\n");
        }
        break;
    case MG_EV_MQTT_PUBACK:
        fprintf(stderr, "MQTT Message publishing acknowledged (msg_id: %d)\n", msg->message_id);
        break;
    case MG_EV_MQTT_SUBACK:
        fprintf(stderr, "MQTT Subscription acknowledged.\n");
        break;
    case MG_EV_MQTT_PUBLISH: {
        fprintf(stderr, "MQTT Incoming message %.*s: %.*s\n", (int)msg->topic.len,
                msg->topic.p, (int)msg->payload.len, msg->payload.p);
        break;
    }
    case MG_EV_CLOSE:
        if (!ctx)
            break; // shuttig down
        if (ctx->prev_status == 0)
            fprintf(stderr, "MQTT Connection failed...\n");
        // reconnect
        if (mg_connect(nc->mgr, ctx->address, mqtt_client_event) == NULL) {
            fprintf(stderr, "MQTT connect(%s) failed\n", ctx->address);
        }
        break;
    }
}

static struct mg_mgr *mqtt_client_init(char const *host, char const *port, char const *user, char const *pass, char const *client_id, int retain)
{
    struct mg_mgr *mgr = calloc(1, sizeof(*mgr));
    if (!mgr) {
        fprintf(stderr, "calloc() failed in %s() %s:%d\n", __func__, __FILE__, __LINE__);
        exit(1);
    }

    mqtt_client_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        fprintf(stderr, "calloc() failed in %s() %s:%d\n", __func__, __FILE__, __LINE__);
        exit(1);
    }
    ctx->opts.user_name = user;
    ctx->opts.password  = pass;
    ctx->publish_flags  = MG_MQTT_QOS(0) | (retain ? MG_MQTT_RETAIN : 0);
    // TODO: these should be user configurable options
    //ctx->opts.keepalive = 60;
    //ctx->timeout = 10000L;
    //ctx->cleansession = 1;
    strncpy(ctx->client_id, client_id, sizeof(ctx->client_id));

    mg_mgr_init(mgr, ctx);

    // if the host is an IPv6 address it needs quoting
    if (strchr(host, ':'))
        snprintf(ctx->address, sizeof(ctx->address), "[%s]:%s", host, port);
    else
        snprintf(ctx->address, sizeof(ctx->address), "%s:%s", host, port);

    if (mg_connect(mgr, ctx->address, mqtt_client_event) == NULL) {
        fprintf(stderr, "MQTT connect(%s) failed\n", ctx->address);
        exit(1);
    }

    return mgr;
}

static int mqtt_client_poll(struct mg_mgr *mgr)
{
    return mg_mgr_poll(mgr, 0);
}

static void mqtt_client_publish(struct mg_mgr *mgr, char const *topic, char const *str)
{
    mqtt_client_t *ctx = (mqtt_client_t *)mgr->user_data;
    ctx->message_id++;

    for (struct mg_connection *c = mg_next(mgr, NULL); c != NULL; c = mg_next(mgr, c)) {
        if (c->proto_handler)
            mg_mqtt_publish(c, topic, ctx->message_id, ctx->publish_flags, str, strlen(str));
    }
}

static void mqtt_client_free(struct mg_mgr *mgr)
{
    free(mgr->user_data);
    mgr->user_data = NULL;
    mg_mgr_free(mgr);
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

typedef enum {
    USE_CHANNEL_NO = 0,
    USE_CHANNEL_REPLACE_ID = 1,
    USE_CHANNEL_AFTER_ID,
    USE_CHANNEL_BEFORE_ID,
} use_channel_t;

typedef struct {
    struct data_output output;
    struct mg_mgr *mgr;
    char topic[256];
    char *devices;
    char *events;
    char *states;
    use_channel_t use_channel;
    //char *homie;
    //char *hass;
} data_output_mqtt_t;

static void print_mqtt_array(data_output_t *output, data_array_t *array, char *format)
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
        *topic++ = '/';
        strcpy(topic, data->value);
        mqtt_sanitize_topic(topic);
        topic += strlen(data->value);
    }
    else if (data->type == DATA_INT) {
        *topic++ = '/';
        topic += sprintf(topic, "%d", *(int *)data->value);
    }
    else {
        fprintf(stderr, "Can't append data type %d to topic\n", data->type);
    }

    return topic;
}

// <prefix>/[<type>/][<model>/][<subtype>/][<channel>|<id>/]battery: "OK"|"LOW"
static void print_mqtt_data(data_output_t *output, data_t *data, char *format)
{
    data_output_mqtt_t *mqtt = (data_output_mqtt_t *)output;

    char *orig = mqtt->topic + strlen(mqtt->topic); // save current topic
    char *end  = orig;

    // collect well-known top level keys
    data_t *data_type    = NULL;
    data_t *data_brand   = NULL;
    data_t *data_model   = NULL;
    data_t *data_subtype = NULL;
    data_t *data_channel = NULL;
    data_t *data_id      = NULL;
    for (data_t *d = data; d; d = d->next) {
        if (!strcmp(d->key, "type"))
            data_type = d;
        else if (!strcmp(d->key, "brand"))
            data_brand = d;
        else if (!strcmp(d->key, "model"))
            data_model = d;
        else if (!strcmp(d->key, "subtype"))
            data_subtype = d;
        else if (!strcmp(d->key, "channel"))
            data_channel = d;
        else if (!strcmp(d->key, "id"))
            data_id = d;
    }

    // top-level only
    if (!*mqtt->topic) {
        // "states" topic
        if (!data_model) {
            if (mqtt->states) {
                size_t message_size = 20000; // state message need a large buffer
                char *message       = malloc(message_size);
                data_print_jsons(data, message, message_size);
                mqtt_client_publish(mqtt->mgr, mqtt->states, message);
                free(message);
            }
            return;
        }

        // "events" topic
        if (mqtt->events) {
            char message[1024]; // we expect the biggest strings to be around 500 bytes.
            data_print_jsons(data, message, sizeof(message));
            mqtt_client_publish(mqtt->mgr, mqtt->events, message);
        }

        // "devices" topic
        if (!mqtt->devices) {
            return;
        }

        strcpy(mqtt->topic, mqtt->devices);
        end = mqtt->topic + strlen(mqtt->topic);
    }

    // create topic
    if (data_type)
        end = append_topic(end, data_type);
    if (data_brand)
        end = append_topic(end, data_brand);
    if (data_model)
        end = append_topic(end, data_model);
    if (data_subtype)
        end = append_topic(end, data_subtype);

    if (mqtt->use_channel == USE_CHANNEL_REPLACE_ID) {
        if (data_channel)
            end = append_topic(end, data_channel);
        else if (data_id)
            end = append_topic(end, data_id);
    }
    else if (mqtt->use_channel == USE_CHANNEL_AFTER_ID) {
        if (data_id)
            end = append_topic(end, data_id);
        if (data_channel)
            end = append_topic(end, data_channel);
    }
    else if (mqtt->use_channel == USE_CHANNEL_BEFORE_ID) {
        if (data_channel)
            end = append_topic(end, data_channel);
        if (data_id)
            end = append_topic(end, data_id);
    }
    else /* USE_CHANNEL_NO */ {
        if (data_id)
            end = append_topic(end, data_id);
    }

    while (data) {
        if (!strcmp(data->key, "time")
                || !strcmp(data->key, "type")
                || !strcmp(data->key, "brand")
                || !strcmp(data->key, "model")
                || !strcmp(data->key, "subtype")) {
            // skip, except "id", "channel"
        }
        else {
            // push topic
            *end = '/';
            strcpy(end + 1, data->key);
            print_value(output, data->type, data->value, data->format);
            *end = '\0'; // pop topic
        }
        data = data->next;
    }
    *orig = '\0'; // restore topic
}

static void print_mqtt_string(data_output_t *output, char const *str, char *format)
{
    data_output_mqtt_t *mqtt = (data_output_mqtt_t *)output;
    mqtt_client_publish(mqtt->mgr, mqtt->topic, str);
}

static void print_mqtt_double(data_output_t *output, double data, char *format)
{
    char str[20];
    int ret = snprintf(str, 20, "%f", data);
    print_mqtt_string(output, str, format);
}

static void print_mqtt_int(data_output_t *output, int data, char *format)
{
    char str[20];
    int ret = snprintf(str, 20, "%d", data);
    print_mqtt_string(output, str, format);
}

static void data_output_mqtt_poll(data_output_t *output)
{
    data_output_mqtt_t *mqtt = (data_output_mqtt_t *)output;

    if (!mqtt)
        return;

    mqtt_client_poll(mqtt->mgr);
}

static void data_output_mqtt_free(data_output_t *output)
{
    data_output_mqtt_t *mqtt = (data_output_mqtt_t *)output;

    if (!mqtt)
        return;

    free(mqtt->devices);
    free(mqtt->events);
    free(mqtt->states);
    //free(mqtt->homie);
    //free(mqtt->hass);

    mqtt_client_free(mqtt->mgr);
    free(mqtt);
}

static char *mqtt_topic_default(char const *topic, char const *base, char const *suffix)
{
    if (topic)
        return strdup(topic);

    if (!base)
        return strdup(suffix);

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", base, suffix);
    return strdup(path);
}

struct data_output *data_output_mqtt_create(char const *host, char const *port, char *opts, char const *dev_hint)
{
    data_output_mqtt_t *mqtt = calloc(1, sizeof(data_output_mqtt_t));
    if (!mqtt) {
        fprintf(stderr, "calloc() failed in %s() %s:%d\n", __func__, __FILE__, __LINE__);
        exit(1);
    }

    char hostname[64];
    gethostname(hostname, sizeof(hostname) - 1);
    hostname[sizeof(hostname) - 1] = '\0';
    // only use hostname, not domain part
    char *dot = strchr(hostname, '.');
    if (dot)
        *dot = '\0';
    //fprintf(stderr, "Hostname: %s\n", hostname);

    // generate a short deterministic client_id to identify this input device on restart
    uint16_t host_crc = crc16((uint8_t *)hostname, strlen(hostname), 0x1021, 0xffff);
    uint16_t devq_crc = crc16((uint8_t *)dev_hint, dev_hint ? strlen(dev_hint) : 0, 0x1021, 0xffff);
    char client_id[17];
    snprintf(client_id, sizeof(client_id), "rtl_433-%04x%04x", host_crc, devq_crc);

    // default base topic
    char base_topic[8 + sizeof(hostname)];
    snprintf(base_topic, sizeof(base_topic), "rtl_433/%s", hostname);

    char *user = NULL;
    char *pass = NULL;
    int retain = 0;

    // defaults
    mqtt->use_channel = USE_CHANNEL_REPLACE_ID;

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
        // Simple key-topic mapping
        else if (!strcasecmp(key, "d") || !strcasecmp(key, "devices"))
            mqtt->devices = mqtt_topic_default(val, base_topic, "devices");
        else if (!strcasecmp(key, "c") || !strcasecmp(key, "usechannel")) {
            if (!strcasecmp(val, "afterid"))
                mqtt->use_channel = USE_CHANNEL_AFTER_ID;
            else if (!strcasecmp(val, "beforeid"))
                mqtt->use_channel = USE_CHANNEL_BEFORE_ID;
            else if (!strcasecmp(val, "replaceid"))
                mqtt->use_channel = USE_CHANNEL_REPLACE_ID;
            else
                mqtt->use_channel = atobv(val, USE_CHANNEL_REPLACE_ID);
        }
        // JSON events to single topic
        else if (!strcasecmp(key, "e") || !strcasecmp(key, "events"))
            mqtt->events = mqtt_topic_default(val, base_topic, "events");
        // JSON states to single topic
        else if (!strcasecmp(key, "s") || !strcasecmp(key, "states"))
            mqtt->states = mqtt_topic_default(val, base_topic, "states");
        // TODO: Homie Convention https://homieiot.github.io/
        //else if (!strcasecmp(key, "o") || !strcasecmp(key, "homie"))
        //    mqtt->homie = mqtt_topic_default(val, NULL, "homie"); // base topic
        // TODO: Home Assistant MQTT discovery https://www.home-assistant.io/docs/mqtt/discovery/
        //else if (!strcasecmp(key, "a") || !strcasecmp(key, "hass"))
        //    mqtt->hass = mqtt_topic_default(val, NULL, "homeassistant"); // discovery prefix
        else {
            printf("Invalid key \"%s\" option.\n", key);
            exit(1);
        }
    }

    // Default is to use all formats
    if (!mqtt->devices && !mqtt->events && !mqtt->states) {
        mqtt->devices = mqtt_topic_default(NULL, base_topic, "devices");
        mqtt->events  = mqtt_topic_default(NULL, base_topic, "events");
        mqtt->states  = mqtt_topic_default(NULL, base_topic, "states");
    }

    mqtt->output.print_data   = print_mqtt_data;
    mqtt->output.print_array  = print_mqtt_array;
    mqtt->output.print_string = print_mqtt_string;
    mqtt->output.print_double = print_mqtt_double;
    mqtt->output.print_int    = print_mqtt_int;
    mqtt->output.output_poll  = data_output_mqtt_poll;
    mqtt->output.output_free  = data_output_mqtt_free;

    mqtt->mgr = mqtt_client_init(host, port, user, pass, client_id, retain);

    return &mqtt->output;
}
