/** @file
    REST (HTTP) output for rtl_433 events

    Copyright (C) 2020 Ville Hukkamäki

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_OUTPUT_HTTP_H_
#define INCLUDE_OUTPUT_HTTP_H_

#include "data.h"

struct data_output *data_output_rest_create(char const *url, int header_count, char *headers[]);

#endif /* INCLUDE_OUTPUT_HTTP_H_ */
