/** @file
    Raw I/Q data output handler.

    Copyright (C) 2022 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "raw_output.h"

#include <stdint.h>

/* generic raw_output */

void raw_output_frame(struct raw_output *output, uint8_t const *data, uint32_t len)
{
    if (!output)
        return;
    output->output_frame(output, data, len);
}

void raw_output_free(struct raw_output *output)
{
    if (!output)
        return;
    output->output_free(output);
}
