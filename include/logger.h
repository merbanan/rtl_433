/** @file
    Basic logging, API.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_LOGGER_H_
#define INCLUDE_LOGGER_H_

// Defined in newer <sal.h> for MSVC.
#ifndef _Printf_format_string_
#define _Printf_format_string_
#endif

/// Log levels, copied from and compatible with SoapySDR.
/// LOG_FATAL, LOG_ERROR, LOG_WARNING are abnormal program states, other levels are normal information.
/// LOG_FATAL is not actually used, fatal errors usually fprintf and terminate.
typedef enum log_level {
    LOG_FATAL    = 1, //!< A fatal error. The application will most likely terminate. This is the highest priority.
    LOG_CRITICAL = 2, //!< A critical error. The application might not be able to continue running successfully.
    LOG_ERROR    = 3, //!< An error. An operation did not complete successfully, but the application as a whole is not affected.
    LOG_WARNING  = 4, //!< A warning. An operation completed with an unexpected result.
    LOG_NOTICE   = 5, //!< A notice, which is an information with just a higher priority.
    LOG_INFO     = 6, //!< An informational message, usually denoting the successful completion of an operation.
    LOG_DEBUG    = 7, //!< A debugging message.
    LOG_TRACE    = 8, //!< A tracing message. This is the lowest priority.
} log_level_t;

typedef void (*r_logger_handler)(log_level_t level, char const *src, char const *msg, void *userdata);

/** Set the log handler.

    @param handler the handler to use, NULL to reset to default handler
    @param userdata user data passed back to the handler
*/
void r_logger_set_log_handler(r_logger_handler const handler, void *userdata);

/** Log a message string.

    @param level a log level
    @param src the log source, typically the function name ("__func__") or a module ("SoapySDR")
    @param msg a log message
*/
void print_log(log_level_t level, char const *src, char const *msg);

/** Log a message format string.

    Be terse, messages should be shorter than 100 and a maximum length of 200 characters.

    @param level a log level
    @param src the log source, typically the function name ("__func__") or a module ("SoapySDR")
    @param fmt a log message format string
*/
void print_logf(log_level_t level, char const *src, _Printf_format_string_ char const *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;

#endif /* INCLUDE_LOGGER_H_ */
