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

#if defined _MSC_VER || defined ESP32 // Microsoft Visual Studio or ESP32
    // MSC and ESP32 have something like C99 restrict as __restrict
    #ifndef restrict
    #define restrict  __restrict
    #endif
#endif
// Defined in newer <sal.h> for MSVC.
#ifndef _Printf_format_string_
#define _Printf_format_string_
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

int abuf_printf(abuf_t *buf, _Printf_format_string_ char const *restrict format, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

#endif /* INCLUDE_ABUF_H_ */
