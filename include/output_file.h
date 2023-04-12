/** @file
    File outputs for rtl_433 events.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_OUTPUT_FILE_H_
#define INCLUDE_OUTPUT_FILE_H_

#include "data.h"
#include <stdio.h>

/** Construct data output for CSV printer.

    @param file the output stream
    @return The auxiliary data to pass along with data_csv_printer to data_print.
            You must release this object with data_output_free once you're done with it.
*/
struct data_output *data_output_csv_create(int log_level, FILE *file);

struct data_output *data_output_json_create(int log_level, FILE *file);

struct data_output *data_output_kv_create(int log_level, FILE *file);

#endif /* INCLUDE_OUTPUT_FILE_H_ */
