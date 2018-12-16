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

#ifndef INCLUDE_TERM_CTL_H_
#define INCLUDE_TERM_CTL_H_

#include <stdio.h>

int term_get_columns(int fd);

int term_has_color(FILE *fp);

void term_init(FILE *fp);

void term_ring_bell(FILE *fp);

typedef enum term_color {
    TERM_COLOR_RESET          = 0,
    TERM_COLOR_BLACK          = 30,
    TERM_COLOR_RED            = 31,
    TERM_COLOR_GREEN          = 32,
    TERM_COLOR_YELLOW         = 33,
    TERM_COLOR_BLUE           = 34,
    TERM_COLOR_MAGENTA        = 35,
    TERM_COLOR_CYAN           = 36,
    TERM_COLOR_WHITE          = 37,
    TERM_COLOR_BRIGHT_BLACK   = 90,
    TERM_COLOR_BRIGHT_RED     = 91,
    TERM_COLOR_BRIGHT_GREEN   = 92,
    TERM_COLOR_BRIGHT_YELLOW  = 93,
    TERM_COLOR_BRIGHT_BLUE    = 94,
    TERM_COLOR_BRIGHT_MAGENTA = 95,
    TERM_COLOR_BRIGHT_CYAN    = 96,
    TERM_COLOR_BRIGHT_WHITE   = 97,
} term_color_t;

void term_set_fg(FILE *fp, term_color_t color);

void term_set_bg(FILE *fp, term_color_t color);

#endif /* INCLUDE_TERM_CTL_H_ */
