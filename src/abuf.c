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

#ifdef _TEST
#define ASSERT_EQUALS(a, b) \
    do { \
        if ((a) == (b)) \
            ++passed; \
        else { \
            ++failed; \
            fprintf(stderr, "FAIL: %d <> %d\n", (int)(a), (int)(b)); \
        } \
    } while (0)
#define ASSERT_STR(a, b) \
    do { \
        if (strcmp((a), (b)) == 0) \
            ++passed; \
        else { \
            ++failed; \
            fprintf(stderr, "FAIL: \"%s\" <> \"%s\"\n", (a), (b)); \
        } \
    } while (0)

int main(void)
{
    unsigned passed = 0;
    unsigned failed = 0;
    abuf_t b;
    char buf[16];

    fprintf(stderr, "abuf:: test\n");

    // A plain append that fits the whole string and no overflow, and returns
    // the source length (excluding the NUL), like abuf_printf().
    fprintf(stderr, "abuf::abuf_cat(): fits\n");
    abuf_init(&b, buf, sizeof(buf));
    ASSERT_EQUALS(abuf_cat(&b, "abc"), 3);
    ASSERT_STR(buf, "abc");
    ASSERT_EQUALS(b.overflow, 0);

    // Boundary: an exact fit (len + 1 == avail, the reserved NUL lands on the
    // last byte) keeps the whole string and is NOT flagged as overflow.
    fprintf(stderr, "abuf::abuf_cat(): exact fit\n");
    char four[4];
    abuf_init(&b, four, sizeof(four));
    ASSERT_EQUALS(abuf_cat(&b, "abc"), 3);
    ASSERT_STR(four, "abc");
    ASSERT_EQUALS(b.overflow, 0);

    // Boundary: one byte over (len + 1 > avail) truncates to fit, always
    // NUL-terminates, sets overflow, and still returns the would-be length.
    fprintf(stderr, "abuf::abuf_cat(): one over\n");
    char three[3];
    abuf_init(&b, three, sizeof(three));
    ASSERT_EQUALS(abuf_cat(&b, "abc"), 3);
    ASSERT_STR(three, "ab");
    ASSERT_EQUALS(b.overflow, 1);

    // Regression for the get_stats JSON corruption: once a chunk is truncated,
    // a later smaller chunk must NOT slip into the leftover room. The result is
    // a clean prefix and overflow stays sticky.
    fprintf(stderr, "abuf::abuf_cat(): no append after truncation\n");
    abuf_init(&b, buf, sizeof(buf));
    abuf_cat(&b, "0123456789ABCDEF"); // 16 chars into a 16-byte buf: truncates
    ASSERT_EQUALS(b.overflow, 1);
    ASSERT_STR(buf, "0123456789ABCDE"); // last byte reserved for the NUL
    abuf_cat(&b, "}");                  // the small closing chunk must not land
    ASSERT_STR(buf, "0123456789ABCDE");
    ASSERT_EQUALS(b.overflow, 1);

    // A zero-length buffer can hold nothing, not even the NUL: every append
    // overflows and writes nothing, but still reports the source length.
    fprintf(stderr, "abuf::abuf_cat(): zero-length buffer\n");
    abuf_init(&b, buf, 0);
    ASSERT_EQUALS(abuf_cat(&b, "x"), 1);
    ASSERT_EQUALS(b.overflow, 1);

    fprintf(stderr, "abuf:: test (%u passed, %u failed)\n", passed, failed);
    return failed;
}
#endif /* _TEST */
