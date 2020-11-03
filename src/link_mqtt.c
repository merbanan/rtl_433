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

#include "fatal.h"

#include "mongoose.h"

/* MQTT client abstraction */

typedef struct mqtt_client {
    struct mg_send_mqtt_handshake_opts opts;
    struct mg_connection *conn;
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

mqtt_client_t *mqtt_client_init(struct mg_mgr *mgr, char const *host, char const *port, char const *user, char const *pass, char const *client_id, int retain)
{
    mqtt_client_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        FATAL_CALLOC("mqtt_client_init()");

    ctx->opts.user_name = user;
    ctx->opts.password  = pass;
    ctx->publish_flags  = MG_MQTT_QOS(0) | (retain ? MG_MQTT_RETAIN : 0);
    // TODO: these should be user configurable options
    //ctx->opts.keepalive = 60;
    //ctx->timeout = 10000L;
    //ctx->cleansession = 1;
    strncpy(ctx->client_id, client_id, sizeof(ctx->client_id));
    ctx->client_id[sizeof(ctx->client_id) - 1] = '\0';

    // if the host is an IPv6 address it needs quoting
    if (strchr(host, ':'))
        snprintf(ctx->address, sizeof(ctx->address), "[%s]:%s", host, port);
    else
        snprintf(ctx->address, sizeof(ctx->address), "%s:%s", host, port);

    struct mg_connect_opts opts = {.user_data = ctx};
    ctx->conn = mg_connect_opt(mgr, ctx->address, mqtt_client_event, opts);
    if (!ctx->conn) {
        fprintf(stderr, "MQTT connect(%s) failed\n", ctx->address);
        exit(1);
    }

    return ctx;
}

void mqtt_client_publish(mqtt_client_t *ctx, char const *topic, char const *str)
{
    if (!ctx->conn || !ctx->conn->proto_handler)
        return;

    ctx->message_id++;
    mg_mqtt_publish(ctx->conn, topic, ctx->message_id, ctx->publish_flags, str, strlen(str));
}

void mqtt_client_free(mqtt_client_t *ctx)
{
    if (ctx && ctx->conn) {
        ctx->conn->user_data = NULL;
        ctx->conn->flags |= MG_F_CLOSE_IMMEDIATELY;
    }
    free(ctx);
}
