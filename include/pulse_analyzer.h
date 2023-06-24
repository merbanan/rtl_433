/** @file
    Pulse analyzer functions.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_PULSE_ANALYZER_H_
#define INCLUDE_PULSE_ANALYZER_H_

#include "pulse_detect.h"

struct r_device;

/// Analyze and print result.
void pulse_analyzer(pulse_data_t *data, int package_type, struct r_device *device);

#endif /* INCLUDE_PULSE_ANALYZER_H_ */
