/** @file
    Raw I/Q data output handler.

    Copyright (C) 2022 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_RAW_OUTPUT_H_
#define INCLUDE_RAW_OUTPUT_H_

#include <stdint.h>

struct raw_output;

typedef struct raw_output {
    void (*output_frame)(struct raw_output *output, uint8_t const *data, uint32_t len);
    void (*output_free)(struct raw_output *output);
} raw_output_t;

void raw_output_frame(struct raw_output *output, uint8_t const *data, uint32_t len);

void raw_output_free(struct raw_output *output);

#endif /* INCLUDE_RAW_OUTPUT_H_ */
