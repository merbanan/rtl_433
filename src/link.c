#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#include "link.h"

/* link helper functions */

link_output_t *link_create_output(link_t *l)
{
    assert(l && l->create_output);

    return l->create_output(l);
}

/* link output helper functions */

int link_output_write(link_output_t *lo, const void *buf, size_t len)
{
    if (lo && lo->write)
        return lo->write(lo, buf, len);
    else
        return 0;
}

int link_output_write_char(link_output_t *lo, const char data)
{
    if (lo && lo->write)
        return lo->write(lo, &data, 1);
    else
        return 0;
}

int link_output_printf(link_output_t *lo, const char *fmt, ...)
{
    va_list ap;
    int r;

    if (!lo || !lo->vprintf)
        return 0;

    va_start(ap, fmt);
    r = lo->vprintf(lo, fmt, ap);
    va_end(ap);

    return r;
}

FILE *link_output_get_stream(link_output_t *lo)
{
    if (lo && lo->get_stream)
        return lo->get_stream(lo);
    else
        return NULL;
}

void link_output_flush(link_output_t *lo)
{
    if (lo && lo->flush)
        lo->flush(lo);
}

void link_output_free(link_output_t *lo)
{
    if (lo && lo->free)
        lo->free(lo);
}
