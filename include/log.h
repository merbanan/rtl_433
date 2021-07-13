/** @file
    Basic logging, internal.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_LOG_H_
#define INCLUDE_LOG_H_

#include "logger.h"

/// Log a format string message of level LOG_TRACE.
#define log_trace(...) r_logger_logf(LOG_TRACE, LOG_MODULE, __FILE__, __LINE__, __func__, __VA_ARGS__)
/// Log a format string message of level LOG_DEBUG.
#define log_debug(...) r_logger_logf(LOG_DEBUG, LOG_MODULE, __FILE__, __LINE__, __func__, __VA_ARGS__)
/// Log a format string message of level LOG_INFO.
#define log_info(...) r_logger_logf(LOG_INFO, LOG_MODULE, __FILE__, __LINE__, __func__, __VA_ARGS__)
/// Log a format string message of unknown level.
#define log_idk(...) r_logger_logf(LOG_INFO, LOG_MODULE, __FILE__, __LINE__, __func__, __VA_ARGS__)
/// Log a format string message of level LOG_WARNING.
#define log_warn(...) r_logger_logf(LOG_WARNING, LOG_MODULE, __FILE__, __LINE__, __func__, __VA_ARGS__)
/// Log a format string message of level LOG_ERROR.
#define log_error(...) r_logger_logf(LOG_ERROR, LOG_MODULE, __FILE__, __LINE__, __func__, __VA_ARGS__)
/// Log a format string message of level LOG_FATAL.
#define log_fatal(...) r_logger_logf(LOG_FATAL, LOG_MODULE, __FILE__, __LINE__, __func__, __VA_ARGS__)

// Defined in newer <sal.h> for MSVC.
#ifndef _Printf_format_string_
#define _Printf_format_string_
#endif

// shim until fprintf is converted to log_
#include <stdio.h>
#define fprintf(...) r_logger_fprintf(LOG_INFO, LOG_MODULE, __FILE__, __LINE__, __func__, __VA_ARGS__)

int r_logger_fprintf(int level, char const *mod, char const *file, int line, char const *func, FILE *stream, _Printf_format_string_ const char *format, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 7, 8)))
#endif
        ;


#endif /* INCLUDE_LOG_H_ */
