/** @file
    RfRaw format functions.

    Copyright (C) 2020 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_RFRAW_H_
#define INCLUDE_RFRAW_H_

#include "pulse_detect.h"
#include <stdbool.h>

/// Check if a given string is in RfRaw format.
bool rfraw_check(char const *p);

/// Decode RfRaw string to pulse data.
bool rfraw_parse(pulse_data_t *data, char const *p);

#endif /* INCLUDE_RFRAW_H_ */
