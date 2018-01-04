/*
 * Publish MQTT topics using Mosquitto or Paho.
 *
 * Copyright (C) 2017 by Christian Zuckschwerdt <zany@triq.ne>
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

#include <stdio.h>
#include <stdlib.h>
#define _XOPEN_SOURCE 500
#include <string.h>

#include "limits.h"
// gethostname() needs _XOPEN_SOURCE 500
#include <unistd.h>

#include "mqtt_pub.h"

// TODO: maybe we should use MQTTAsync.h, though MQTTClient.h is threaded with MQTTClient_setCallbacks().

// clean the topic inplace to [-.A-Za-z0-9], exp. not whitespace, +, #, /, $
char *mqtt_pub_sanitize_topic(char *topic)
{
    for (char *p = topic; *p; ++p)
        if (*p != '-' && *p != '.' && (*p < 'A' || *p > 'Z') && (*p < 'a' || *p > 'z') && (*p < '0' || *p > '9'))
            *p = '_';

    return topic;
}

void mqtt_pub_publish(mqtt_pub_t *mqtt, const char *str)
{
    int r;
    fprintf(stderr, "MQTT publish topic %s with %s\n", mqtt->topic, str);

#if defined(HAVE_MOSQUITTO)

    int mid;
    r = mosquitto_publish(mqtt->client, &mid, mqtt->topic, strlen(str), str, mqtt->qos, mqtt->retain);
    if (r != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTT publish topic failed, return code %d\n", r);
    }

#elif defined(HAVE_PAHO)

    MQTTClient_deliveryToken token;
    r = MQTTClient_publish(mqtt->client, mqtt->topic, strlen(str), str, mqtt->qos, mqtt->retain, &token);
    if (r != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "MQTT publish topic failed, return code %d\n", r);
    }

#else

    fprintf(stderr, "Cant't publish, MQTT not available.\n");

#endif
}

#if defined(HAVE_MOSQUITTO)

static void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    if (rc == 0) { /* success */
        fprintf(stderr, "MQTT connected successfully\n");
    } else {
        fprintf(stderr, "MQTT connect failed (%d)\n", rc);
    }
}

static void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    if (rc == 0) { /* success */
        fprintf(stderr, "MQTT disconnected successfully\n");
    } else {
        fprintf(stderr, "MQTT disconnected unexpectedly (%d)\n", rc);
        // default is to reconnect with a delay of 1 second until the connection succeeds.
    }
}

static void on_publish(struct mosquitto *mosq, void *obj, int mid)
{
    fprintf(stderr, "MQTT published successfully (%d)\n", mid);
}

#elif defined(HAVE_PAHO)

static void on_connectionLost(void *context, char *cause)
{
    fprintf(stderr, "MQTT disconnected unexpectedly (%s)\n", cause);
    // initiated reconnect
}

static int on_messageArrived(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    // ignore
    return 1;
}

static void on_deliveryComplete(void *context, MQTTClient_deliveryToken dt)
{
    fprintf(stderr, "MQTT published successfully (%d)\n", dt);
}

#endif

mqtt_pub_t *mqtt_pub_init(const char *host, int port)
{
    int r;

    if (!host || !port) {
        fprintf(stderr, "MQTT: host or port missing!\n");
        return NULL;
    }

    mqtt_pub_t *mqtt = calloc(1, sizeof(mqtt_pub_t));
    if (!mqtt) {
        fprintf(stderr, "MQTT: calloc() failed\n");
        return NULL;
    }

    char hostname[_POSIX_HOST_NAME_MAX + 1];
    gethostname(hostname, _POSIX_HOST_NAME_MAX + 1);
    hostname[_POSIX_HOST_NAME_MAX] = '\0';
    char *dot = strchr(hostname, '.');
    if (dot) *dot = '\0';
    fprintf(stderr, "Hostname: %s\n", hostname);

    mqtt->clientid = strdup("Rtl433ClientPub");
    snprintf(mqtt->topic, 256, "rtl_433/%s", hostname);
    mqtt->qos = 1;
    mqtt->retain = 0; // maybe 1 after testing
    mqtt->timeout = 10000L;

    const int keepalive = 20;
    const int cleansession = 1;
    const int verbose = 1;

#if defined(HAVE_MOSQUITTO)

    mosquitto_lib_init();
    mqtt->client = mosquitto_new(mqtt->clientid, cleansession, NULL);

    mosquitto_connect_callback_set(mqtt->client, on_connect);
    mosquitto_disconnect_callback_set(mqtt->client, on_disconnect);
    if (verbose) {
        mosquitto_publish_callback_set(mqtt->client, on_publish);
    }

    r = mosquitto_loop_start(mqtt->client);
    if (r != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTT thread start failed!\n");
        exit(1);
    }

    r = mosquitto_connect_async(mqtt->client, host, port, keepalive);
    if (r != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "MQTT connect failed!\n");
        exit(1);
    }

#elif defined(HAVE_PAHO)

    char address[256];
    if (strchr(host, ':')) {
        snprintf(address, 256, "tcp://[%s]:%d", host, port);
    } else {
        snprintf(address, 256, "tcp://%s:%d", host, port);
    }

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

    MQTTClient_create(&mqtt->client, address, mqtt->clientid, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = keepalive;
    conn_opts.cleansession = cleansession;

    if (verbose) {
        r = MQTTClient_setCallbacks(mqtt->client, NULL, on_connectionLost, on_messageArrived, on_deliveryComplete);
    } else {
        r = MQTTClient_setCallbacks(mqtt->client, NULL, on_connectionLost, on_messageArrived, NULL);
    }
    if (r != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "MQTT set callbacks failed, return code %d\n", r);
        exit(1);
    }

    r = MQTTClient_connect(mqtt->client, &conn_opts);
    if (r != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "MQTT connect failed, return code %d\n", r);
        exit(1);
    }

#else

    fprintf(stderr, "Cant't init, MQTT not available.\n");

#endif

    return mqtt;
}

void mqtt_pub_free(mqtt_pub_t *mqtt)
{
    int r;

#if defined(HAVE_MOSQUITTO)

    if (mqtt->client) {
        r = mosquitto_disconnect(mqtt->client);
        if (r != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "MQTT disconnect failed!\n");
        }

        r = mosquitto_loop_stop(mqtt->client, 0);
        if (r != MOSQ_ERR_SUCCESS) {
            fprintf(stderr, "MQTT stop thread failed!\n");
        }

        mosquitto_destroy(mqtt->client);
        mosquitto_lib_cleanup();
    }

#elif defined(HAVE_PAHO)

    if (mqtt->client) {
        r = MQTTClient_disconnect(mqtt->client, mqtt->timeout);
        if (r != MQTTCLIENT_SUCCESS) {
            fprintf(stderr, "MQTT disconnect failed!\n");
        }
        MQTTClient_destroy(&mqtt->client);
    }

#else

    fprintf(stderr, "Cant't free, MQTT not available.\n");

#endif

    if (mqtt->clientid)
        free(mqtt->clientid);

    free(mqtt);
}
