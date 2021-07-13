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
static r_logger_handler aux_handler    = NULL;
static void *aux_handler_userdata      = NULL;

// shim until fprintf is converted to log_
#undef fprintf

static void default_handler(log_level_t level, char const *mod, char const *file, int line, char const *func, char const *msg)
{
    (void)file;
    (void)line;
    if (msg && msg[strlen(msg) - 1] == '\n') {
        fprintf(stderr, "%s(%d) %s: %s", mod, level, func, msg);
    }
    else {
        fprintf(stderr, "%s(%d) %s: %s\n", mod, level, func, msg);
    }
}

void r_logger_set_log_handler(r_logger_handler const handler, void *userdata)
{
    logger_handler = handler;
    logger_handler_userdata = userdata;
}

void r_logger_set_aux_handler(r_logger_handler const handler, void *userdata)
{
    aux_handler = handler;
    aux_handler_userdata = userdata;
}

void r_logger_log(log_level_t level, char const *mod, char const *file, int line, char const *func, char const *msg)
{
    if (logger_handler) {
        logger_handler(level, mod, file, line, func, msg, logger_handler_userdata);
    }
    else {
        default_handler(level, mod, file, line, func, msg);
    }
    if (aux_handler) {
        aux_handler(level, mod, file, line, func, msg, aux_handler_userdata);
    }
}

void r_logger_logf(log_level_t level, char const *mod, char const *file, int line, char const *func, char const *fmt, ...)
{
    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    r_logger_log(level, mod, file, line, func, msg);
}

int r_logger_fprintf(log_level_t level, char const *mod, char const *file, int line, char const *func, FILE *stream, const char *format, ...)
{
    if (stream != stderr) {
        va_list ap;
        va_start(ap, format);
        int r = vfprintf(stream, format, ap);
        va_end(ap);
        return r;
    }

    int r;
    if (!logger_handler) {
        va_list ap;
        va_start(ap, format);
        r = vfprintf(stream, format, ap);
        va_end(ap);
    }
    if (logger_handler || aux_handler) {
        char msg[4096];
        va_list ap;
        va_start(ap, format);
        r = vsnprintf(msg, sizeof(msg), format, ap);
        va_end(ap);

        // cap length to match fprintf return
        r = r >= (int)sizeof(msg) ? (int)sizeof(msg) - 1 : r;
        // strip trailing newline from typical fprintf
        if (r > 0 && msg[r - 1] == '\n') {
            msg[r - 1] = '\0';
        }

        if (logger_handler) {
            logger_handler(level, mod, file, line, func, msg, logger_handler_userdata);
        }
        if (aux_handler) {
            aux_handler(level, mod, file, line, func, msg, aux_handler_userdata);
        }

    }
    return r;
}
