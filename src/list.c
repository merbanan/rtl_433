/**
 * Generic list
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "list.h"
#include <stdlib.h>

void list_ensure_size(list_t *list, size_t min_size)
{
    if (!list->elems || list->size < min_size) {
        list->elems = realloc(list->elems, min_size * sizeof(*list->elems));
        list->size  = min_size;

        list->elems[list->len] = NULL; // ensure a terminating NULL
    }
}

void list_push(list_t *list, void *p)
{
    if (list->len + 1 >= list->size) // account for terminating NULL
        list_ensure_size(list, list->size < 8 ? 8 : list->size + list->size / 2);

    list->elems[list->len++] = p;

    list->elems[list->len] = NULL; // ensure a terminating NULL
}

void list_free(list_t *list)
{
    if (list->len) {
        for (void **iter = list->elems; iter && *iter; ++iter)
            free(*iter);
        list->len = 0;

        free(list->elems);
        list->elems = NULL;
        list->size  = 0;
    }
}
