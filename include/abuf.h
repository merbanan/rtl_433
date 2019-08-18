/** @file
    array buffer (string builder).

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_ABUF_H_
#define INCLUDE_ABUF_H_

#if defined _MSC_VER // Microsoft Visual Studio
 // MSC has something like C99 restrict as __restrict
#ifndef restrict
#define restrict  __restrict
#endif
#endif

#include <stddef.h>

typedef struct abuf {
    char *head;
    char *tail;
    size_t left;
} abuf_t;

void abuf_init(abuf_t *buf, char *dst, size_t len);

void abuf_setnull(abuf_t *buf);

char *abuf_push(abuf_t *buf);

void abuf_pop(abuf_t *buf, char *end);

void abuf_cat(abuf_t *buf, const char *str);

int abuf_printf(abuf_t *buf, const char *restrict format, ...);

#endif /* INCLUDE_ABUF_H_ */
