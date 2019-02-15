/** @file
    Terminal control utility functions.

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_TERM_CTL_H_
#define INCLUDE_TERM_CTL_H_

#include <stdio.h>

void *term_init(FILE *fp);

void term_free(void *ctx);

int term_get_columns(void *ctx);

int term_has_color(void *ctx);

void term_ring_bell(void *ctx);

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

void term_set_fg(void *ctx, term_color_t color);

void term_set_bg(void *ctx, term_color_t color);

/*
 * Defined in newer <sal.h> for MSVC.
 */
#ifndef _Printf_format_string_
#define _Printf_format_string_
#endif

/**
 * Print to terminal with color-codes inline turned into above colors.
 * Takes a var-arg format.
 *
 * E.g.:
 *   void *term = term_init(stdout);
 *   term_printf (term, "~4Hello ~2world~0.\n");
 *
 *   will print to stdout with 'Hello' mapped to colour 4
 *   and 'world' mapped to colour 2. See 'term_set_color_map()' below.
 *
 * And a 'term_printf (NULL, "~4Hello ~2world~0.\n");'
 * will print "Hello world" to stder' with no colors.
 */
int term_printf(void *ctx, _Printf_format_string_ const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__ ((format(printf,2,3)))
#endif
    ;

/**
 * Like 'term_printf()', but no var-arg format.
 * Simply takes a 0-terminated buffer.
 */
int term_puts(void *ctx, const char *buf);

/**
 * Change the default color map.
 * By default, the color-codes maps to these foreground colour:
 *   "~0": always restores terminal-colors; TERM_COLOR_RESET.
 *   "~1": print using TERM_COLOR_GREEN.
 *   "~2": print using TERM_COLOR_WHITE.
 *   "~3": print using TERM_COLOR_BLUE.
 *   "~4": print using TERM_COLOR_CYAN.
 *   "~5": print using TERM_COLOR_MAGENTA.
 *   "~6": print using TERM_COLOR_YELLOW.
 *   "~7": print using TERM_COLOR_BLACK.
 *   "~8": print using TERM_COLOR_RED.
 */
int term_set_color_map(int idx, term_color_t color);

/**
 * Returns the current color-value ('enum term_color') for color-index.
 * 'idx'. This index goes from ASCII '0' to 'X'.
 * 'X' = '0' + the dimension of the internal 'color_map[]'.
 */
int term_get_color_map(int idx);

#endif /* INCLUDE_TERM_CTL_H_ */
