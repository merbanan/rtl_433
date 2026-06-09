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
    buf->overflow = 0;
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

int abuf_cat(abuf_t *buf, char const *str)
{
    size_t len   = strlen(str);
    size_t avail = buf->left;

    // snprintf semantics: copy as much as fits and always NUL-terminate, rather
    // than dropping the whole chunk on overflow. The old all-or-nothing variant
    // left room behind a dropped chunk so the next, smaller append still landed,
    // producing structurally invalid JSON.
    if (avail > 0) {
        size_t copy = len < avail - 1 ? len : avail - 1;
        memcpy(buf->tail, str, copy);
        buf->tail += copy;
        buf->left -= copy;
        *buf->tail = '\0';
    }
    // The whole string plus its terminating NUL must fit; an exact fit (len + 1
    // == avail) is not an overflow, so the reserved NUL keeps "full" distinct
    // from "truncated".
    if (len + 1 > avail) {
        buf->overflow = 1;
    }

    // Return the would-be length (excluding the NUL), like abuf_printf().
    return (int)len;
}

int abuf_printf(abuf_t *buf, _Printf_format_string_ char const *restrict format, ...)
{
    va_list ap;
    va_start(ap, format);

    size_t avail = buf->left;
    int n = vsnprintf(buf->tail, buf->left, format, ap);

    if (n > 0) {
        size_t len = (size_t)n < buf->left ? (size_t)n : buf->left;
        buf->tail += len;
        buf->left -= len;
    }
    // vsnprintf returns the length it *would* have written (excluding the NUL).
    // n >= avail means the string (or its terminating NUL) did not fit.
    if (n < 0 || (size_t)n >= avail) {
        buf->overflow = 1;
    }

    va_end(ap);
    return n;
}
