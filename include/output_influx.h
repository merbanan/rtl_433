/** @file
    InfluxDB output for rtl_433 events

    Copyright (C) 2019 Daniel Krueger
    based on output_mqtt.c
    Copyright (C) 2019 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_OUTPUT_INFLUX_H_
#define INCLUDE_OUTPUT_INFLUX_H_

#include "data.h"

struct data_output *data_output_influx_create(char *opts);

#endif /* INCLUDE_OUTPUT_INFLUX_H_ */
