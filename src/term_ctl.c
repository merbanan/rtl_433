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

#ifdef _WIN32
#include <io.h>
#include <limits.h>
#include <windows.h>

#ifndef STDOUT_FILENO
#define STDOUT_FILENO   1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO   2
#endif

static CONSOLE_SCREEN_BUFFER_INFO console_info;
static BOOL                       console_redirected = FALSE;
static HANDLE                     console_hnd = NULL;
static FILE                      *console_file = NULL;
static WORD                       active_fg, active_bg;

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

static void _term_set_color(BOOL fore, term_color_t color)
{
    WORD win_color;

    if (!console_file)
        return;

    if (color == TERM_COLOR_RESET) {
        active_fg = (console_info.wAttributes & 7);
        active_bg = (console_info.wAttributes & ~7);
    }
    else if (fore) {
        active_fg = _term_get_win_color(TRUE,color);
    }
    else if (color <= TERM_COLOR_WHITE) {
        active_bg = 16 * _term_get_win_color(FALSE,color);
    }
    else
        return;

    win_color = active_bg + active_fg;

    /* Hack: as WinCon does not have color-themes (as Linux have) and no 'TERM_COLOR_BRIGHT_x'
     * codes are used, always use a high-intensity foreground color. This look best in
     * CMD with the default black background color.
     */
    if (fore && color != TERM_COLOR_RESET)
        win_color |= FOREGROUND_INTENSITY;

    fflush(console_file);
    SetConsoleTextAttribute(console_hnd, win_color);
}

/*
 * Cleanup in case we got a SIGINT signal in the middle of a
 * non-default colour output.
 */
static void _term_exit (void)
{
    if (console_hnd && console_hnd != INVALID_HANDLE_VALUE) {
        fflush(console_file);
        SetConsoleTextAttribute(console_hnd, console_info.wAttributes);
    }
}

static BOOL _term_init(FILE *fp)
{
    int fd;

    if (console_hnd)
        return (!console_redirected);

    fd = fileno(fp);
    if (fd == STDOUT_FILENO) {
        console_hnd = GetStdHandle(STD_OUTPUT_HANDLE);
        console_file = fp;
    }
    else if (fd == STDERR_FILENO) {
        console_hnd = GetStdHandle(STD_ERROR_HANDLE);
        console_file = fp;
    }
    console_redirected = (console_hnd == INVALID_HANDLE_VALUE) ||
                         (!GetConsoleScreenBufferInfo(console_hnd, &console_info)) ||
                         (GetFileType(console_hnd) != FILE_TYPE_CHAR);

    _term_set_color(FALSE, TERM_COLOR_RESET); /* Set 'active_fg' and 'active_bg' */
    atexit(_term_exit);
    return (!console_redirected);
}
#endif /* _WIN32 */

int term_get_columns(int fd)
{
#ifdef _WIN32
    if (fd == fileno(console_file)) { /* Should we 'assert()' for this? */
       /*
        * Call this again as the screen dimensions could have changes since
        * we called '_term_init()'.
        */
       CONSOLE_SCREEN_BUFFER_INFO c_info;

       GetConsoleScreenBufferInfo(console_hnd, &c_info);
       return (c_info.srWindow.Right - c_info.srWindow.Left + 1);
    }
    return (INT_MAX);
#else
    struct winsize w;
    ioctl(fd, TIOCGWINSZ, &w);
    return w.ws_col;
#endif
}

int term_has_color(FILE *fp)
{
#ifdef _WIN32
    return _term_init(fp);
#else
    return isatty(fileno(fp)); // || get_env("force_color")
#endif
}

void term_init(FILE *fp)
{
#ifdef _WIN32
    _term_init(fp);
#endif
}

void term_ring_bell(FILE *fp)
{
#ifdef _WIN32
    Beep(800, 20);
#else
    fprintf(fp, "\a");
#endif
}

void term_set_fg(FILE *fp, term_color_t color)
{
#ifdef _WIN32
   _term_set_color(TRUE, color);
#else
    if (color == TERM_COLOR_RESET)
        fprintf(fp, "\033[0m");
    else
        fprintf(fp, "\033[%d;1m", color);
#endif
}

void term_set_bg(FILE *fp, term_color_t color)
{
#ifdef _WIN32
    _term_set_color(FALSE, color);
#else
    if (color == TERM_COLOR_RESET)
        fprintf(fp, "\033[0m");
    else
        fprintf(fp, "\033[%d;1m", color + 10);
#endif
}
