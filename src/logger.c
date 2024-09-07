/** @file
    Basic logging.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <logger.h>

static r_logger_handler logger_handler = NULL;
static void *logger_handler_userdata   = NULL;

static void default_handler(log_level_t level, char const *src, char const *msg)
{
    (void)level;
    fprintf(stderr, "%s: %s\n", src, msg);
}

void r_logger_set_log_handler(r_logger_handler const handler, void *userdata)
{
    logger_handler = handler;
    logger_handler_userdata = userdata;
}

void print_log(log_level_t level, char const *src, char const *msg)
{
    if (logger_handler) {
        logger_handler(level, src, msg, logger_handler_userdata);
    }
    else {
        default_handler(level, src, msg);
    }
}

void print_logf(log_level_t level, char const *src, char const *fmt, ...)
{
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    print_log(level, src, msg);
}
