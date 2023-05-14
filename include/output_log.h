/** @file
    Log outputs for rtl_433 events.

    Copyright (C) 2022 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_OUTPUT_LOG_H_
#define INCLUDE_OUTPUT_LOG_H_

#include "data.h"
#include <stdio.h>

/** Construct data output for LOG printer.

    @param file the optional output stream, defaults to stderr
    @return The auxiliary data to pass along with data_log_printer to data_print.
            You must release this object with data_output_free once you're done with it.
*/
struct data_output *data_output_log_create(int log_level, FILE *file);

#endif /* INCLUDE_OUTPUT_LOG_H_ */
