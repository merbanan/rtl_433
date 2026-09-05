/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2026 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "timer.h"

#include <stdio.h>
#include <time.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#define usleep(us) Sleep((us) / 1000)
#endif

void interval_timer_init(interval_timer_t *interval_timer)
{
    // set to current wall clock
    int ret = gettimeofday(interval_timer, NULL);
    if (ret) {
        perror("gettimeofday");
    }
}

void interval_timer_wait(interval_timer_t *interval_timer, unsigned interval_us)
{
    // sync to wall clock
    struct timeval now_tv;
    int ret = gettimeofday(&now_tv, NULL);
    if (ret) {
        perror("gettimeofday");
    }

    time_t elapsed_s  = now_tv.tv_sec - interval_timer->tv_sec;
    time_t elapsed_us = 1000000 * elapsed_s + now_tv.tv_usec - interval_timer->tv_usec;

    // set next wanted start time
    interval_timer->tv_usec += interval_us;
    while (interval_timer->tv_usec > 1000000) {
        interval_timer->tv_usec -= 1000000;
        interval_timer->tv_sec += 1;
    }

    if ((time_t)interval_us > elapsed_us) {
        usleep(interval_us - elapsed_us);
    }
}
