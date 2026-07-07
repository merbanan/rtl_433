/*
 * A general structure for extracting hierarchical data from the devices;
 * typically key-value pairs, but allows for more rich data as well
 *
 * Copyright (C) 2015 by Erkki Seppälä <flux@modeemi.fi>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data.h"
#include "output_file.h"

// Regression test for the get_stats JSON truncation bug: a report whose size
// scales with configuration (here, a large array of device sub-objects, as the
// HTTP server's get_stats builds for many enabled decoders) must serialize to
// complete, valid JSON. The old fixed-buffer path silently corrupted the output
// once it overflowed; data_print_jsons_dup() grows the buffer to fit.
static int test_jsons_large_report(void)
{
    int failed = 0;
    int n      = 230; // more than enough to exceed any reasonable fixed buffer

    data_t **objs = calloc(n, sizeof(*objs));
    for (int i = 0; i < n; ++i) {
        objs[i] = data_make(
                "device",       "", DATA_INT,    i,
                "name",         "", DATA_STRING, "Some Reasonably Long Decoder Name For A Protocol",
                "events",       "", DATA_INT,    8256,
                "abort_length", "", DATA_INT,    8256,
                NULL);
    }
    data_t *data = data_make(
            "enabled", "", DATA_INT,   n,
            "stats",   "", DATA_ARRAY, data_array(n, DATA_DATA, objs),
            NULL);

    // The grown buffer must hold the whole document: starts with '{', ends with
    // '}', and is far larger than the smallest fixed buffer we test below.
    char *full = data_print_jsons_dup(data);
    if (!full) {
        fprintf(stderr, "FAIL: data_print_jsons_dup returned NULL\n");
        failed++;
    }
    else {
        size_t full_len = strlen(full);
        if (full[0] != '{' || full[full_len - 1] != '}') {
            fprintf(stderr, "FAIL: grown JSON is not a complete object: ...%.20s\n",
                    full + (full_len > 20 ? full_len - 20 : 0));
            failed++;
        }
        if (full_len <= 20480) {
            fprintf(stderr, "FAIL: test report (%zu bytes) too small to exercise growth\n", full_len);
            failed++;
        }

        // Confirm the failure mode the fix addresses: a fixed buffer that is too
        // small yields a truncated document that does NOT end with '}'.
        char small[1024];
        size_t w = data_print_jsons(data, small, sizeof(small));
        if (w > 0 && w <= sizeof(small) && small[w - 1] == '}') {
            fprintf(stderr, "FAIL: undersized buffer unexpectedly produced a complete object\n");
            failed++;
        }
    }

    free(full);
    free(objs);
    data_free(data);
    return failed;
}

int main(void)
{
    /* clang-format off */
    data_t *data = data_make(
            "label",        "",             DATA_STRING, "1.2.3",
            "house_code",   "House Code",   DATA_INT,    42,
            "temp",         "Temperature",  DATA_DOUBLE, 99.9,
            "array",        "Array",        DATA_ARRAY, data_array(2, DATA_STRING, (char*[2]){"hello", "world"}),
            "array2",       "Array 2",      DATA_ARRAY, data_array(2, DATA_INT, (int[2]){4, 2}),
            "array3",       "Array 3",      DATA_ARRAY, data_array(2, DATA_ARRAY, (data_array_t*[2]){
                                                            data_array(2, DATA_INT, (int[2]){4, 2}),
                                                            data_array(2, DATA_INT, (int[2]){5, 5}) }),
            "data",         "Data",        DATA_DATA, data_make("Hello", "hello", DATA_STRING, "world", NULL),
            NULL);
    /* clang-format on */
    const char *fields[] = { "label", "house_code", "temp", "array", "array2", "array3", "data", "house_code" };

    void *json_output = data_output_json_create(0, stdout);
    void *kv_output = data_output_kv_create(0, stdout);
    void *csv_output = data_output_csv_create(0, stdout);
    data_output_start(csv_output, fields, sizeof fields / sizeof *fields);

    data_output_print(json_output, data); fprintf(stdout, "\n");
    data_output_print(kv_output, data);
    data_output_print(csv_output, data);

    data_output_free(json_output);
    data_output_free(kv_output);
    data_output_free(csv_output);

    data_free(data);

    return test_jsons_large_report();
}
