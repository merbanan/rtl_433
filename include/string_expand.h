/** @file
    String formatting a-la MQTT topic

    Copyright (C) 2019 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "data.h"

typedef char *(expand_string_sanitizer)(char *, char *);

char *expand_topic_string(char *topic, char const *format, data_t *data, char const *hostname, expand_string_sanitizer sanitizer);
