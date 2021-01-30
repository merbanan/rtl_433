/** @file
    Custom data tags for data struct.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "data_tag.h"
#include "data.h"
#include "optparse.h"
#include "fileformat.h"
#include "fatal.h"

data_tag_t *data_tag_create(char *param)
{
    data_tag_t *tag;
    tag = calloc(1, sizeof(data_tag_t));
    if (!tag) {
        WARN_CALLOC("data_tag_create()");
        return NULL;
    }

    tag->prepend = 1; // always prepend tag
    tag->val = param;
    tag->key = asepc(&tag->val, '=');
    if (!tag->val) {
        tag->val = tag->key;
        tag->key = "tag";
    }

    return tag; // NOTE: returns NULL on alloc failure.
}

void data_tag_free(data_tag_t *tag)
{
    free(tag);
}

data_t *data_tag_apply(data_tag_t *tag, data_t *data, char const *filename)
{
    char const *val = tag->val;
    if (filename && !strcmp("PATH", tag->val)) {
        val = filename;
    }
    else if (filename && !strcmp("FILE", tag->val)) {
        val = file_basename(filename);
    }
    data = data_prepend(data,
            tag->key, "", DATA_STRING, val,
            NULL);

    return data;
}
