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

void link_output_write(link_output_t *lo, const void *buf, size_t len)
{
    if (lo && lo->write)
        lo->write(lo, buf, len);
}

void link_output_write_char(link_output_t *lo, const char data)
{
    if (lo && lo->write)
        lo->write(lo, &data, 1);
}

void link_output_printf(link_output_t *lo, const char *fmt, ...)
{
    va_list ap;

    if (!lo || !lo->vprintf)
        return;

    va_start(ap, fmt);
    lo->vprintf(lo, fmt, ap);
    va_end(ap);
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
