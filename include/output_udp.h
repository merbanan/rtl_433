/** @file
    UDP syslog output for rtl_433 events.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_OUTPUT_UDP_H_
#define INCLUDE_OUTPUT_UDP_H_

#include "data.h"

struct data_output *data_output_syslog_create(int log_level, const char *host, const char *port);

#endif /* INCLUDE_OUTPUT_UDP_H_ */
