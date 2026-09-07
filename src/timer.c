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

#ifdef _WIN32

static void _get_monotonic_time(interval_timer_t *interval_timer)
{
    *interval_timer = GetTickCount(); // number of milliseconds that have elapsed since the system was started, wraps every 49.7 days.
}

time_t monotonic_time()
{
    // if we had C11 atomics (https://en.cppreference.com/c/language/atomic) we could increment an overflow counter on each (atomic) wrap around.
    // e.g.
    // static atomic_ulong last_tick = ATOMIC_VAR_INIT(0);
    // static unsigned long wrap_around = 0;

    // number of milliseconds that have elapsed since the system was started, wraps every 49.7 days.
    // note that 64-bit Windows is LLP64 and not LP64 as 64-bit unix-like systems.
    unsigned long t = GetTickCount();

    // unsigned long p = atomic_exchange_explicit(&last_tick, t, memory_order_relaxed);
    // if (p > t) {
    //     wrap_around += 1;
    // }

    // return ((unsigned long long)t + (unsigned long long)wrap_around * (ULONG_MAX + 1)) / 1000;
    return t / 1000;
}

time_t monotonic_time_diff(time_t time1, time_t time0)
{
    unsigned long time0_ms = time0 * 1000; // expand to fit the wrap around
    unsigned long time1_ms = time1 * 1000; // expand to fit the wrap around
    unsigned long d_ms = time1_ms - time0_ms;
    return d_ms / 1000;
}

#else

static void _get_monotonic_time(interval_timer_t *interval_timer)
{
    int r = clock_gettime(CLOCK_MONOTONIC, interval_timer);
    if (r) {
        perror("clock_gettime(CLOCK_MONOTONIC)");
    }
}

time_t monotonic_time(void)
{
    struct timespec t;
    int r = clock_gettime(CLOCK_MONOTONIC, &t);
    if (r) {
        perror("clock_gettime(CLOCK_MONOTONIC)");
    }
    return t.tv_sec; // number of seconds that have elapsed since some arbitrary time.
}

time_t monotonic_time_diff(time_t time1, time_t time0)
{
    return time1 - time0;
}

#endif

void interval_timer_init(interval_timer_t *interval_timer)
{
    _get_monotonic_time(interval_timer);
}

void interval_timer_wait(interval_timer_t *interval_timer, unsigned interval_us)
{
    // sync to wall clock
    interval_timer_t now_tv;
    _get_monotonic_time(&now_tv);

    time_t elapsed_s  = now_tv.tv_sec - interval_timer->tv_sec;
    time_t elapsed_ns = 1000000000 * elapsed_s + now_tv.tv_nsec - interval_timer->tv_nsec;

    //time_t diff = elapsed_ns / interval_us; // TODO: finish skip logic
    //time_t skip = diff * interval_us;       // TODO: finish skip logic
    // set next wanted start time
    interval_timer->tv_nsec += interval_us * 1000;
    while (interval_timer->tv_nsec > 1000000000) {
        interval_timer->tv_nsec -= 1000000000;
        interval_timer->tv_sec += 1;
    }

    // sleep the remaining time to interval_us
    if ((time_t)interval_us * 1000 > elapsed_ns) {
        usleep(interval_us - elapsed_ns / 1000);
    }
}
