/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2026 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_TIMER_H_
#define INCLUDE_TIMER_H_

#include "compat_time.h"

typedef struct timeval interval_timer_t;

void interval_timer_init(interval_timer_t *interval_timer);

void interval_timer_wait(interval_timer_t *interval_timer, unsigned interval_us);

#endif /* INCLUDE_TIMER_H_ */
