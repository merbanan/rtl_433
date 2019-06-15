/** @file
    array buffer (string builder).

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#include "abuf.h"

void abuf_init(abuf_t *buf, char *dst, size_t len)
{
    buf->head = dst;
    buf->tail = dst;
    buf->left = len;
}

void abuf_setnull(abuf_t *buf)
{
    buf->head = NULL;
    buf->tail = NULL;
    buf->left = 0;
}

char *abuf_push(abuf_t *buf)
{
    return buf->tail;
}

void abuf_pop(abuf_t *buf, char *end)
{
    buf->left += buf->tail - end;
    buf->tail = end;
}

void abuf_cat(abuf_t *buf, const char *str)
{
    size_t len = strlen(str);
    if (buf->left >= len + 1) {
        strcpy(buf->tail, str);
        buf->tail += len;
        buf->left -= len;
    }
}

int abuf_printf(abuf_t *buf, const char *restrict format, ...)
{
    va_list ap;
    va_start(ap, format);

    int n = vsnprintf(buf->tail, buf->left, format, ap);

    size_t len = 0;
    if (n > 0) {
        len = (size_t)n < buf->left ? (size_t)n : buf->left;
        buf->tail += len;
        buf->left -= len;
    }

    va_end(ap);
    return n;
}
