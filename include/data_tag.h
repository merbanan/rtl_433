/** @file
    Custom data tags for data struct.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_TAGS_H_
#define INCLUDE_TAGS_H_

struct gpsd_client;
struct mg_mgr;
struct data;

typedef struct data_tag {
    char const *key;
    char const *val;
    char const **includes;
    struct gpsd_client *gpsd_client;
} data_tag_t;

/// Create a data tag. Might fail and return NULL.
data_tag_t *data_tag_create(char *params, struct mg_mgr *mgr);

/// Free a data tag.
void data_tag_free(data_tag_t *tag);

/// Apply a data tag.
struct data *data_tag_apply(data_tag_t *tag, struct data *data, char const *filename);

#endif /* INCLUDE_TAGS_H_ */
