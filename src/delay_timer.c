/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2026 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include "delay_timer.h"

#ifdef _WIN32
#include <windows.h>
#define usleep(us) Sleep((us) / 1000)
#endif

void delay_timer_init(delay_timer_t *delay_timer)
{
    // set to current wall clock
    int ret = gettimeofday(delay_timer, NULL);
    if (ret) {
        perror("gettimeofday");
    }
}

void delay_timer_wait(delay_timer_t *delay_timer, unsigned delay_us)
{
    // sync to wall clock
    struct timeval now_tv;
    int ret = gettimeofday(&now_tv, NULL);
    if (ret) {
        perror("gettimeofday");
    }

    time_t elapsed_s  = now_tv.tv_sec - delay_timer->tv_sec;
    time_t elapsed_us = 1000000 * elapsed_s + now_tv.tv_usec - delay_timer->tv_usec;

    // set next wanted start time
    delay_timer->tv_usec += delay_us;
    while (delay_timer->tv_usec > 1000000) {
        delay_timer->tv_usec -= 1000000;
        delay_timer->tv_sec += 1;
    }

    if ((time_t)delay_us > elapsed_us) {
        usleep(delay_us - elapsed_us);
    }
}
