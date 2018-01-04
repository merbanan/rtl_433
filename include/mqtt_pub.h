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

#ifndef INCLUDE_MQTT_PUB_H_
#define INCLUDE_MQTT_PUB_H_

#if defined(HAVE_MOSQUITTO)
    #include "mosquitto.h"
#elif defined(HAVE_PAHO)
    #include "MQTTClient.h"
#endif

typedef struct {
#if defined(HAVE_MOSQUITTO)
    struct mosquitto *client;
#elif defined(HAVE_PAHO)
    MQTTClient client;
#endif
    char *clientid;
    char topic[256];
    int qos;
    int retain;
    int timeout;
} mqtt_pub_t;

mqtt_pub_t *mqtt_pub_init(const char *host, int port);

void mqtt_pub_free(mqtt_pub_t *mqtt);

void mqtt_pub_publish(mqtt_pub_t *mqtt, const char *str);

char *mqtt_pub_sanitize_topic(char *topic);

#endif // INCLUDE_MQTT_PUB_H_
