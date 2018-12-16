/**
 * Terminal control utility functions.
 *
 * Copyright (C) 2018 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#include "term_ctl.h"

int term_get_columns(int fd)
{
#ifndef _WIN32
    struct winsize w;
    ioctl(fd, TIOCGWINSZ, &w);
    return w.ws_col;
#else
    return 80; // default
#endif
}

int term_has_color(FILE *fp)
{
#ifndef _WIN32
    return isatty(fileno(fp)); // || get_env("force_color")
#else
    return 0; // default
#endif
}

void term_init(FILE *fp)
{
#ifndef _WIN32
    // nothing to do
#else
    // ...
#endif
}

void term_ring_bell(FILE *fp)
{
#ifndef _WIN32
    fprintf(fp, "\a");
#else
    // nop
#endif
}

void term_set_fg(FILE *fp, term_color_t color)
{
#ifndef _WIN32
    if (color == TERM_COLOR_RESET)
        fprintf(fp, "\033[0m");
    else
        fprintf(fp, "\033[%d;1m", color);
#else
    // nop
#endif
}

void term_set_bg(FILE *fp, term_color_t color)
{
#ifndef _WIN32
    if (color == TERM_COLOR_RESET)
        fprintf(fp, "\033[0m");
    else
        fprintf(fp, "\033[%d;1m", color + 10);
#else
    // nop
#endif
}
