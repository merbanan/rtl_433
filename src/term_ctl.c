/** @file
    Terminal control utility functions.

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#include "term_ctl.h"

#ifdef _WIN32
#include <stdlib.h>
#include <io.h>
#include <limits.h>
#include <windows.h>

#ifndef STDOUT_FILENO
#define STDOUT_FILENO   1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO   2
#endif

typedef struct console {
    CONSOLE_SCREEN_BUFFER_INFO info;
    BOOL                       redirected;
    HANDLE                     hnd;
    FILE                      *file;
    WORD                       fg, bg;
} console_t;

static WORD _term_get_win_color(BOOL fore, term_color_t color)
{
    switch (color) {
       case TERM_COLOR_RESET: /* Cannot occur; just to supress a warning */
            break;
       case TERM_COLOR_BLACK:
       case TERM_COLOR_BRIGHT_BLACK:
            return (0);
       case TERM_COLOR_BLUE:
            return (1);
       case TERM_COLOR_GREEN:
            return (2);
       case TERM_COLOR_CYAN:
            return (3);
       case TERM_COLOR_RED:
            return (4);
       case TERM_COLOR_MAGENTA:
            return (5);
       case TERM_COLOR_YELLOW:
            return (6);
       case TERM_COLOR_WHITE:
            return (7);
       case TERM_COLOR_BRIGHT_BLUE:
            return (1 + FOREGROUND_INTENSITY);
       case TERM_COLOR_BRIGHT_GREEN:
            return (2 + FOREGROUND_INTENSITY);
       case TERM_COLOR_BRIGHT_CYAN:
            return (3 + FOREGROUND_INTENSITY);
       case TERM_COLOR_BRIGHT_RED:
            return (4 + FOREGROUND_INTENSITY);
       case TERM_COLOR_BRIGHT_MAGENTA:
            return (5 + FOREGROUND_INTENSITY);
       case TERM_COLOR_BRIGHT_YELLOW:
            return (6 + FOREGROUND_INTENSITY);
       case TERM_COLOR_BRIGHT_WHITE:
            return (7 + FOREGROUND_INTENSITY);
    }
    fprintf(stderr,"FATAL: No mapping for TERM_COLOR_x=%d (fore: %d)\n", color, fore);
    return (0);
}

static void _term_set_color(console_t *console, BOOL fore, term_color_t color)
{
    WORD win_color;

    if (!console->file)
        return;

    if (color == TERM_COLOR_RESET) {
        console->fg = (console->info.wAttributes & 7);
        console->bg = (console->info.wAttributes & ~7);
    }
    else if (fore) {
        console->fg = _term_get_win_color(TRUE, color);
    }
    else if (color <= TERM_COLOR_WHITE) {
        console->bg = 16 * _term_get_win_color(FALSE, color);
    }
    else
        return;

    win_color = console->bg + console->fg;

    /* Hack: as WinCon does not have color-themes (as Linux have) and no 'TERM_COLOR_BRIGHT_x'
     * codes are used, always use a high-intensity foreground color. This look best in
     * CMD with the default black background color.
     *
     * Note: do not do this for "BLACK" as that would turn it into "GREY".
     */
    if (fore && color != TERM_COLOR_RESET && color != TERM_COLOR_BLACK)
        win_color |= FOREGROUND_INTENSITY;

    fflush(console->file);
    SetConsoleTextAttribute(console->hnd, win_color);
}

/*
 * Cleanup in case we got a SIGINT signal in the middle of a
 * non-default colour output.
 */
static void _term_free(console_t *console)
{
    if (console->hnd && console->hnd != INVALID_HANDLE_VALUE) {
        fflush(console->file);
        SetConsoleTextAttribute(console->hnd, console->info.wAttributes);
    }
    free(console);
}

static BOOL _term_has_color(console_t *console)
{
    return console->hnd && !console->redirected;
}

static void *_term_init(FILE *fp)
{
    console_t *console = calloc(1, sizeof(*console));

    int fd = fileno(fp);
    if (fd == STDOUT_FILENO) {
        console->hnd = GetStdHandle(STD_OUTPUT_HANDLE);
        console->file = fp;
    }
    else if (fd == STDERR_FILENO) {
        console->hnd = GetStdHandle(STD_ERROR_HANDLE);
        console->file = fp;
    }
    console->redirected = (console->hnd == INVALID_HANDLE_VALUE) ||
                         (!GetConsoleScreenBufferInfo(console->hnd, &console->info)) ||
                         (GetFileType(console->hnd) != FILE_TYPE_CHAR);

    _term_set_color(console, FALSE, TERM_COLOR_RESET); /* Set 'console->fg' and 'console->bg' */

    return console;
}
#endif /* _WIN32 */

int term_get_columns(void *ctx)
{
#ifdef _WIN32
    console_t *console = (console_t *)ctx;
    /*
     * Call this again as the screen dimensions could have changes since
     * we called '_term_init()'.
     */
    CONSOLE_SCREEN_BUFFER_INFO c_info;

    if (!console->hnd || console->hnd == INVALID_HANDLE_VALUE)
       return (80);

    if (!GetConsoleScreenBufferInfo(console->hnd, &c_info))
       return (80);
    return (c_info.srWindow.Right - c_info.srWindow.Left + 1);
#else
    FILE *fp = (FILE *)ctx;
    struct winsize w;
    ioctl(fileno(fp), TIOCGWINSZ, &w);
    return w.ws_col;
#endif
}

int term_has_color(void *ctx)
{
#ifdef _WIN32
    return _term_has_color(ctx);
#else
    FILE *fp = (FILE *)ctx;
    return isatty(fileno(fp)); // || get_env("force_color")
#endif
}

void *term_init(FILE *fp)
{
#ifdef _WIN32
    return _term_init(fp);
#else
    return fp;
#endif
}

void term_free(void *ctx)
{
    if (!ctx)
        return;
#ifdef _WIN32
    _term_free(ctx);
#else
    FILE *fp = (FILE *)ctx;
    fprintf(fp, "\033[0m");
#endif
}

void term_ring_bell(void *ctx)
{
#ifdef _WIN32
    Beep(800, 20);
    (void) ctx;
#else
    FILE *fp = (FILE *)ctx;
    fprintf(fp, "\a");
#endif
}

void term_set_fg(void *ctx, term_color_t color)
{
#ifdef _WIN32
   _term_set_color(ctx, TRUE, color);
#else
    FILE *fp = (FILE *)ctx;
    if (color == TERM_COLOR_RESET)
        fprintf(fp, "\033[0m");
    else
        fprintf(fp, "\033[%d;1m", color);
#endif
}

void term_set_bg(void *ctx, term_color_t color)
{
#ifdef _WIN32
    _term_set_color(ctx, FALSE, color);
#else
    FILE *fp = (FILE *)ctx;
    if (color == TERM_COLOR_RESET)
        fprintf(fp, "\033[0m");
    else
        fprintf(fp, "\033[%d;1m", color + 10);
#endif
}

#define DIM(array) (int) (sizeof(array) / sizeof(array[0]))

static term_color_t color_map[] = {
                    TERM_COLOR_RESET,     /* "~0" */
                    TERM_COLOR_GREEN,
                    TERM_COLOR_WHITE,     /* "~2" */
                    TERM_COLOR_BLUE,
                    TERM_COLOR_CYAN,      /* "~4" */
                    TERM_COLOR_MAGENTA,
                    TERM_COLOR_YELLOW,    /* "~6" */
                    TERM_COLOR_BLACK,
                    TERM_COLOR_RED,       /* "~8" */
                  };

int term_set_color_map(int ascii_idx, term_color_t color)
{
    ascii_idx -= '0';
    if (ascii_idx < 0 || ascii_idx > DIM(color_map))
        return -1;
    color_map[ascii_idx] = color;
    return ascii_idx;
}

int term_get_color_map(int ascii_idx)
{
    int i;

    ascii_idx -= '0';
    for (i = 0; ascii_idx >= 0 && i < DIM(color_map); i++)
        if (i == ascii_idx)
           return (int)color_map[i];
    return -1;
}

int term_puts(void *ctx, char const *buf)
{
    char const *p = buf;
    int i, len, buf_len, color;
    FILE *fp;

    if (!ctx)
        fprintf(stderr, "%s", buf);

#ifdef _WIN32
    console_t *console = (console_t *)ctx;
    fp = console->file;
#else
    fp = (FILE *)ctx;
#endif

    if (!fp)
        fp = stderr;

    buf_len = strlen(buf);
    for (i = len = 0; *p && i < buf_len; i++, p++) {
        if (*p != '~') {
            fputc(*p, fp);
            len++;
        }
        else {
            p++;
            color = ctx ? term_get_color_map(*p) : -1;
            if (color >= 0)
                term_set_fg(ctx, (term_color_t)color);
        }
    }
    return len;
}

int term_printf(void *ctx, char const *format, ...)
{
    int len;
    va_list args;
    char buf[4000];

    va_start(args, format);

    // Terminate first in case a buggy '_MSC_VER < 1900' is used.
    buf[sizeof(buf)-1] = '\0';
    vsnprintf(buf, sizeof(buf)-1, format, args);
    len = term_puts(ctx, buf);
    va_end (args);
    return len;
}
