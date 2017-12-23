/* General purpose deocder. WIP.
 *
 * Copyright (C) 2017 Christian Zuckschwerdt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "data.h"
#include "rtl_433.h"
#include "util.h"

// this limits us to only one gdecoder, needs a proper context parameter
static char *myname = NULL;
static int min_rows = 1;
static int min_bits = 1;

static int gdecode_callback(bitbuffer_t *bitbuffer) {
    int i;
    int min_bits_found = 0;
    data_t *data;
    data_t *row_data[BITBUF_ROWS];
    char row_bytes[BITBUF_COLS + 1];
    char time_str[LOCAL_TIME_BUFLEN];

    // discard short / unwanted bitbuffers
    if (bitbuffer->num_rows < min_rows)
        return 0;

    for (i = 0; i < bitbuffer->num_rows; i++) {
        if (bitbuffer->bits_per_row[i] >= min_bits) {
            min_bits_found = 1;
            break;
        }
    }
    if (!min_bits_found)
        return 0;

    // TODO: add some spec for repeats...
    //int r = bitbuffer_find_repeated_row(bitbuffer, 4, 42);
    //if (r < 0 || bitbuffer->bits_per_row[r] != 42)
    //    return 0;

    if (debug_output >= 1) {
        fprintf(stderr, "%s: ", myname);
        bitbuffer_print(bitbuffer);
    }

    local_time_str(0, time_str);
    for (i = 0; i < bitbuffer->num_rows; i++) {
        row_bytes[0] = "\0";
        for (int col = 0; col < (bitbuffer->bits_per_row[i] + 7) / 8; ++col) {
            sprintf(&row_bytes[2 * col], "%02x", bitbuffer->bb[i][col]);
        }
        row_data[i] = data_make(
            "len", "", DATA_INT, bitbuffer->bits_per_row[i],
            "data", "", DATA_STRING, strdup(row_bytes),
            NULL);
    }
    data = data_make(
        "time", "", DATA_STRING, time_str,
        "model", "", DATA_STRING, myname,
        "num_rows", "", DATA_INT, bitbuffer->num_rows,
        "rows", "", DATA_ARRAY, data_array(bitbuffer->num_rows, DATA_DATA, row_data),
        NULL);
    data_acquired_handler(data);

    return 0;
}

static char *output_fields[] = {
    "time",
    "model",
    "bits",
    "num_rows",
    "rows",
    NULL
};

r_device *gdecode_create_device(char *spec) {
    r_device *dev = (r_device *)malloc(sizeof(r_device));
    char *c;

    spec = strdup(spec);
    c = strtok(spec, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad gdecoder spec, missing name!\n");
        exit(1);
    }
    myname = strdup(c);
    snprintf(dev->name, sizeof(dev->name), "General Purpose decoder '%s'", c);

    c = strtok(NULL, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad gdecoder spec, missing modulation!\n");
        exit(1);
    }
    // TODO: add demod_arg where needed
    if (!strcasecmp(c, "OOK_MANCHESTER_ZEROBIT"))
        dev->modulation = OOK_PULSE_MANCHESTER_ZEROBIT;
    else if (!strcasecmp(c, "OOK_PCM_RZ"))
        dev->modulation = OOK_PULSE_PCM_RZ;
    else if (!strcasecmp(c, "OOK_PPM_RAW"))
        dev->modulation = OOK_PULSE_PPM_RAW;
    else if (!strcasecmp(c, "OOK_PWM_PRECISE"))
        dev->modulation = OOK_PULSE_PWM_PRECISE;
    else if (!strcasecmp(c, "OOK_PWM_RAW"))
        dev->modulation = OOK_PULSE_PWM_RAW;
    else if (!strcasecmp(c, "OOK_PWM_TERNARY"))
        dev->modulation = OOK_PULSE_PWM_TERNARY;
    else if (!strcasecmp(c, "OOK_CLOCK_BITS"))
        dev->modulation = OOK_PULSE_CLOCK_BITS;
    else if (!strcasecmp(c, "OOK_PWM_OSV1"))
        dev->modulation = OOK_PULSE_PWM_OSV1;
    else if (!strcasecmp(c, "FSK_PCM"))
        dev->modulation = FSK_PULSE_PCM;
    else if (!strcasecmp(c, "FSK_PWM_RAW"))
        dev->modulation = FSK_PULSE_PWM_RAW;
    else if (!strcasecmp(c, "FSK_MANCHESTER_ZEROBIT"))
        dev->modulation = FSK_PULSE_MANCHESTER_ZEROBIT;
    else {
        fprintf(stderr, "Bad gdecoder spec, unknown modulation!\n");
        exit(1);
    }

    c = strtok(NULL, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad gdecoder spec, missing short limit!\n");
        exit(1);
    }
    dev->short_limit = atoi(c);

    c = strtok(NULL, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad gdecoder spec, missing long limit!\n");
        exit(1);
    }
    dev->long_limit = atoi(c);

    c = strtok(NULL, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad gdecoder spec, missing reset limit!\n");
        exit(1);
    }
    dev->reset_limit = atoi(c);

    c = strtok(NULL, ":");
    if (c != NULL) {
        dev->demod_arg = atoi(c);
    }

    dev->json_callback = &gdecode_callback;
    dev->disabled = 0;
    dev->fields = output_fields;

    c = strtok(NULL, ":");
    if (c != NULL) {
        min_bits = atoi(c);
    }

    free(spec);
    return dev;
}
