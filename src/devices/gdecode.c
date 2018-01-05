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
#include "optparse.h"

struct gdecode {
    char *myname;
    int min_rows;
    int min_bits;
    int min_repeats;
    int match_len;
};

static int gdecode_callback(bitbuffer_t *bitbuffer, struct gdecode *params)
{
    int i;
    int min_bits_found = 0;
    data_t *data;
    data_t *row_data[BITBUF_ROWS];
    char row_bytes[BITBUF_COLS + 1];
    char time_str[LOCAL_TIME_BUFLEN];

    // discard short / unwanted bitbuffers
    if (bitbuffer->num_rows < params->min_rows)
        return 0;

    for (i = 0; i < bitbuffer->num_rows; i++) {
        if (bitbuffer->bits_per_row[i] >= params->min_bits) {
            min_bits_found = 1;
            break;
        }
    }
    if (!min_bits_found)
        return 0;

    int r = bitbuffer_find_repeated_row(bitbuffer, params->min_repeats, params->min_bits);
    if (r < 0)
        return 0;

    // TODO: add some spec for matches...

    if (debug_output >= 1) {
        fprintf(stderr, "%s: ", params->myname);
        bitbuffer_print(bitbuffer);
    }

    local_time_str(0, time_str);
    for (i = 0; i < bitbuffer->num_rows; i++) {
        row_bytes[0] = '\0';
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
        "model", "", DATA_STRING, params->myname,
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

#define GDECODE_SLOTS 8
static struct gdecode *params_slot[GDECODE_SLOTS];
static int cb_slot0(bitbuffer_t *bitbuffer) { return gdecode_callback(bitbuffer, params_slot[0]); }
static int cb_slot1(bitbuffer_t *bitbuffer) { return gdecode_callback(bitbuffer, params_slot[1]); }
static int cb_slot2(bitbuffer_t *bitbuffer) { return gdecode_callback(bitbuffer, params_slot[2]); }
static int cb_slot3(bitbuffer_t *bitbuffer) { return gdecode_callback(bitbuffer, params_slot[3]); }
static int cb_slot4(bitbuffer_t *bitbuffer) { return gdecode_callback(bitbuffer, params_slot[4]); }
static int cb_slot5(bitbuffer_t *bitbuffer) { return gdecode_callback(bitbuffer, params_slot[5]); }
static int cb_slot6(bitbuffer_t *bitbuffer) { return gdecode_callback(bitbuffer, params_slot[6]); }
static int cb_slot7(bitbuffer_t *bitbuffer) { return gdecode_callback(bitbuffer, params_slot[7]); }
static unsigned next_slot = 0;
int (*callback_slot[])(bitbuffer_t *bitbuffer) = {cb_slot0, cb_slot1, cb_slot2, cb_slot3, cb_slot4, cb_slot5, cb_slot6, cb_slot7};

r_device *gdecode_create_device(char *spec)
{
    if (next_slot >= GDECODE_SLOTS) {
        fprintf(stderr, "Maximum number of gdecoder reached!\n");
        exit(1);
    }

    struct gdecode *params = (struct gdecode *)calloc(1, sizeof(struct gdecode));
    params_slot[next_slot] = params;
    r_device *dev = (r_device *)calloc(1, sizeof(r_device));
    char *c;

    spec = strdup(spec);
    c = strtok(spec, ":");
    if (c == NULL) {
        fprintf(stderr, "Bad gdecoder spec, missing name!\n");
        exit(1);
    }
    params->myname = strdup(c);
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

    dev->json_callback = callback_slot[next_slot];
    dev->demod_arg = 0;
    dev->disabled = 0;
    dev->fields = output_fields;

    params->min_rows = 1;
    params->min_bits = 1;
    params->min_repeats = 1;

    getkwargs(&c, NULL, NULL); // skip the initial fixed part
    char *key, *val;
    while (getkwargs(&c, &key, &val)) {
        if (!strcasecmp(key, "demod"))
            dev->demod_arg = atoi(val);

        else if (!strcasecmp(key, "minbits"))
            params->min_bits = atoi(val);

        else if (!strcasecmp(key, "minrows"))
            params->min_rows = atoi(val);

        else if (!strcasecmp(key, "minrepeats"))
            params->min_repeats = atoi(val);

        else if (!strcasecmp(key, "match"))
            params->match_len = atoi(val); // TODO:

        else {
            fprintf(stderr, "Bad gdecoder spec, unknown keyword (%s)!\n", key);
            exit(1);
        }
    }

    free(spec);
    next_slot++;
    return dev;
}
