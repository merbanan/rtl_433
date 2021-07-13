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

/// Log levels, copied from and compatible with SoapySDR.
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

typedef void (*r_logger_handler)(log_level_t level, char const *mod, char const *file, int line, char const *func, char const *msg, void *userdata);

/** Set the log handler.

    @param handler the handler to use, NULL to reset to default handler
    @param userdata user data passed back to the handler
*/
void r_logger_set_log_handler(r_logger_handler const handler, void *userdata);

/** Set an auxilary log handler.

    @param handler the handler to use, NULL to disable
    @param userdata user data passed back to the handler
*/
void r_logger_set_aux_handler(r_logger_handler const handler, void *userdata);

/** Log a message string.

    @param level a log level
    @param mod the module name
    @param file the file name
    @param line the line number
    @param func the function name
    @param msg the log message
*/
void r_logger_log(log_level_t level, char const *mod, char const *file, int line, char const *func, char const *msg);

/** Log a message format string.

    @param level a log level
    @param mod the module name
    @param file the file name
    @param line the line number
    @param func the function name
    @param fmt the log message format string
*/
void r_logger_logf(log_level_t level, char const *mod, char const *file, int line, char const *func, char const *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 6, 7)))
#endif
        ;

#endif /* INCLUDE_LOGGER_H_ */
