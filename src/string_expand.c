/** @file
    String formatting a-la MQTT topic

    Copyright (C) 2019 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "string_expand.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *append_topic(char *topic, data_t *data, expand_string_sanitizer sanitizer)
{
    if (data->type == DATA_STRING) {
        strcpy(topic, data->value.v_ptr); // NOLINT
        size_t len = strlen(topic);
        (*sanitizer)(topic, topic + len);
        topic += len;
    }
    else if (data->type == DATA_INT) {
        topic += sprintf(topic, "%d", data->value.v_int);
    }
    else {
        print_logf(LOG_ERROR, __func__, "Can't append data type %d to topic", data->type);
    }

    return topic;
}

char *expand_topic_string(char *topic, char const *format, data_t *data, char const *hostname, expand_string_sanitizer sanitizer)
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
            topic = append_topic(topic, data_token, sanitizer);
        else if (string_token)
            topic += sprintf(topic, "%s", string_token);
        else
            topic += sprintf(topic, "%.*s", (int)(d_end - d_start), d_start);
    }

    *topic = '\0';
    return topic;
}
