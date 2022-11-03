/** @file
    Trigger output for rtl_433 events.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_OUTPUT_TRIGGER_H_
#define INCLUDE_OUTPUT_TRIGGER_H_

#include "data.h"
#include <stdio.h>

/// Construct data output for a trigger stream.
///
/// This will print a `1` to the stream for every event.
///
/// Use e.g. on a Raspberry Pi to flash the LED:
///
///     $ sudo chmod a+w /sys/class/leds/led0/shot
///     $ echo oneshot | sudo tee /sys/class/leds/led0/trigger
///     $ rtl_433 ... -F trigger:/sys/class/leds/led0/shot
///
/// @param file a trigger output stream
/// @return The initialized data output.
///         You must release this object with data_output_free once you're done with it.
struct data_output *data_output_trigger_create(FILE *file);

#endif /* INCLUDE_OUTPUT_TRIGGER_H_ */
