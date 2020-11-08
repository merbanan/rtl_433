/** @file
    MQTT transport link driver

    Copyright (C) 2019 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "util.h"
#include "link.h"
#include "optparse.h"
#include "fatal.h"

#include "mongoose.h"

/* MQTT client abstraction */

typedef struct {
    link_t base;
    struct mg_send_mqtt_handshake_opts opts;
    struct mg_connection *conn;
    int prev_status;
    char address[253 + 6 + 1]; // dns max + port
    char user_name[256];
    char password[256];
    char client_id[256];
    uint16_t message_id;
} link_mqtt_t;

typedef struct {
    link_output_t base;
    char topic[256];
    struct mbuf buf;
    int publish_flags; // MG_MQTT_RETAIN | MG_MQTT_QOS(0)
} link_mqtt_output_t;

static void mqtt_client_event(struct mg_connection *nc, int ev, void *ev_data)
{
    // note that while shutting down the ctx is NULL
    link_mqtt_t *ctx = (link_mqtt_t *) nc->user_data;
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
        struct mg_connect_opts opts = {.user_data = ctx};
        ctx->conn = mg_connect_opt(nc->mgr, ctx->address, mqtt_client_event, opts);
        if (!ctx->conn) {
            fprintf(stderr, "MQTT connect(%s) failed\n", ctx->address);
        }
        break;
    }
}

/* link output functions */

static int out_write(link_output_t *output, const void *buf, size_t len)
{
    link_mqtt_output_t *out = (link_mqtt_output_t *) output;

    mbuf_append(&out->buf, buf, len);
    return len;
}

static int out_vprintf(link_output_t *output, const char *fmt, va_list ap)
{
    char buf[65536];
    int n;

    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    return out_write(output, buf, MIN(n, sizeof(buf) - 1));
}

static void out_set_destination(link_output_t *output, const char *dest)
{
    link_mqtt_output_t *out = (link_mqtt_output_t *) output;

    snprintf(out->topic, sizeof(out->topic), "%s", dest);
}

static void out_flush(link_output_t *output)
{
    link_mqtt_output_t *out = (link_mqtt_output_t *) output;
    link_mqtt_t *ctx = (link_mqtt_t *) output->link;

    if (out->buf.len > 0 && out->topic[0] != '\0' && ctx->conn && ctx->conn->proto_handler) {
        ctx->message_id++;
        mg_mqtt_publish(ctx->conn, out->topic, ctx->message_id, out->publish_flags, out->buf.buf, out->buf.len);
    }

    mbuf_clear(&out->buf);
}

static void out_free(link_output_t *output)
{
    link_mqtt_output_t *out = (link_mqtt_output_t *) output;

    if (!out)
        return;

    mbuf_free(&out->buf);
    free(out);
}

/* link functions */

static link_output_t *create_output(link_t *link, char *param, list_t *kwargs)
{
    const link_mqtt_output_t template = {.base = {.write = out_write, .vprintf = out_vprintf, .set_destination = out_set_destination, .flush = out_flush, .free = out_free}, .publish_flags = MG_MQTT_QOS(0)};
    link_mqtt_t *ctx = (link_mqtt_t *) link;
    size_t i;
    char *key, *val;
    link_mqtt_output_t *output = malloc(sizeof(link_mqtt_output_t));
    if (!output)
        return NULL;

    *output = template;
    output->base.link = link;
    mbuf_init(&output->buf, 256);

    if (param && param[0] != '\0') {
        out_set_destination(&output->base, param);
    }

    for (i = 0; i < kwargs->len; ) {
        key = kwargs->elems[i];
        val = kwargs->elems[i + 1];
        if (!strcasecmp(key, "r") || !strcasecmp(key, "retain")) {
            if (atobv(val, 1))
                output->publish_flags |= MG_MQTT_RETAIN;
            else
                output->publish_flags &= ~MG_MQTT_RETAIN;
        } else {
            i += 2;
            continue;
        }
        list_remove(kwargs, i, NULL);
        list_remove(kwargs, i, NULL);
    }

    if (kwargs->len > 0) {
        fprintf(stderr, "extra parameters for link %s: %s\n", link->name, (const char *) kwargs->elems[0]);
        out_free(&output->base);
        return NULL;
    }

    return &output->base;
}

static void entry_free(link_t *link)
{
    link_mqtt_t *ctx = (link_mqtt_t *) link;

    if (ctx && ctx->conn) {
        ctx->conn->user_data = NULL;
        ctx->conn->flags |= MG_F_CLOSE_IMMEDIATELY;
    }
    free(ctx);
}

link_t *link_mqtt_create(list_t *links, const char *name, void *mgr, const char *dev_hint, const char *host, const char *port, list_t *kwargs)
{
    const link_mqtt_t template = {.base = {.type = LINK_MQTT, .create_output = create_output, .free = entry_free}};
    link_mqtt_t *ctx, *l;
    size_t i;
    const char *key, *val;

    if (!host || !port || !kwargs) {
        fprintf(stderr, "invalid link parameters\n");
        return NULL;
    }

    ctx = malloc(sizeof(*ctx));
    if (!ctx)
        FATAL_MALLOC("link_mqtt_create()");

    *ctx = template;

    // if the host is an IPv6 address it needs quoting
    if (strchr(host, ':'))
        snprintf(ctx->address, sizeof(ctx->address), "[%s]:%s", host, port);
    else
        snprintf(ctx->address, sizeof(ctx->address), "%s:%s", host, port);

    link_mqtt_generate_client_id(ctx->client_id, sizeof(ctx->client_id), dev_hint);

    for (i = 0; i < kwargs->len; ) {
        key = kwargs->elems[i];
        val = kwargs->elems[i + 1];
        if (!strcasecmp(key, "u") || !strcasecmp(key, "user")) {
            snprintf(ctx->user_name, sizeof(ctx->user_name), "%s", val);
            ctx->opts.user_name = ctx->user_name;
        } else if (!strcasecmp(key, "p") || !strcasecmp(key, "pass")) {
            snprintf(ctx->password, sizeof(ctx->password), "%s", val);
            ctx->opts.password = ctx->password;
        } else {
            i += 2;
            continue;
        }
        list_remove(kwargs, i, NULL);
        list_remove(kwargs, i, NULL);
    }

    // TODO: these should be user configurable options
    //ctx->opts.keepalive = 60;
    //ctx->timeout = 10000L;
    //ctx->cleansession = 1;

    if (name) {
        snprintf(ctx->base.name, sizeof(ctx->base.name), "%s", name);
    } else {
        for (size_t i = 0; i < links->len; ++i) {
            l = links->elems[i];
            if (l->base.type == LINK_MQTT && strcasecmp(ctx->address, l->address) == 0 && strcmp(ctx->user_name, l->user_name) == 0 && strcmp(ctx->password, l->password) == 0 && strcmp(ctx->client_id, l->client_id) == 0) {
                link_free(&ctx->base);
                return &ctx->base;
            }
        }
    }

    struct mg_connect_opts opts = {.user_data = ctx};
    ctx->conn = mg_connect_opt(mgr, ctx->address, mqtt_client_event, opts);
    if (!ctx->conn) {
        fprintf(stderr, "MQTT connect(%s) failed\n", ctx->address);
        link_free(&ctx->base);
        return NULL;
    }

    list_push(links, ctx);

    return &ctx->base;
}

void link_mqtt_generate_client_id(char *client_id, size_t clen, const char *dev_hint)
{
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
    snprintf(client_id, clen, "rtl_433-%04x%04x", host_crc, devq_crc);
}
