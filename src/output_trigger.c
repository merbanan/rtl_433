/** @file
    Trigger output for rtl_433 events.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "output_trigger.h"

#include "data.h"
#include "r_util.h"
#include "fatal.h"

#include <stdio.h>
#include <stdlib.h>

/* Trigger printer */

typedef struct {
    struct data_output output;
    FILE *file;
} data_output_trigger_t;

static void R_API_CALLCONV data_output_trigger_print(data_output_t *output, data_t *data)
{
    UNUSED(data);
    data_output_trigger_t *trigger = (data_output_trigger_t *)output;

    fputc('1', trigger->file);
    fflush(trigger->file);
}

static void R_API_CALLCONV data_output_trigger_free(data_output_t *output)
{
    if (!output)
        return;

    free(output);
}

struct data_output *data_output_trigger_create(FILE *file)
{
    data_output_trigger_t *trigger = calloc(1, sizeof(data_output_trigger_t));
    if (!trigger) {
        WARN_CALLOC("data_output_trigger_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    trigger->output.output_print = data_output_trigger_print;
    trigger->output.output_free  = data_output_trigger_free;
    trigger->file                = file;

    return &trigger->output;
}
