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

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// The handler feeds the outputs, and the HTTP, WebSocket and MQTT outputs go through Mongoose, which is
// only safe from the task that runs its event loop. On ESP-IDF the USB driver's tasks and the acquire
// thread log too, so their lines go to the default handler (the console) instead.
static TaskHandle_t logger_handler_task = NULL;
#endif

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
#ifdef ESP_PLATFORM
    logger_handler_task = xTaskGetCurrentTaskHandle();
#endif
}

void print_log(log_level_t level, char const *src, char const *msg)
{
#ifdef ESP_PLATFORM
    if (logger_handler && xTaskGetCurrentTaskHandle() == logger_handler_task) {
#else
    if (logger_handler) {
#endif
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
