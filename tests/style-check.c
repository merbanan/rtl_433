/** @file
    Source code style checks.

    Copyright (C) 2019 by Christian Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

// Run with: make CTEST_OUTPUT_ON_FAILURE=1 test

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define MAX_LEN 1024

/// Source code file style checks.
/// Check that there are no long lines.
/// Check that there are no CRLF endings.
/// Check that there are no mixed tabs/spaces.
static int style_check(char *path)
{
    char *strict = strstr(path, "/devices/");

    FILE *fp = fopen(path, "r");
    assert(fp);
    if (!fp)
        exit(EXIT_FAILURE);

    int read_errors = 0;
    int long_errors = 0;
    int crlf_errors = 0;
    int tabs_errors = 0;
    int memc_errors = 0;

    int leading_tabs = 0;
    int leading_spcs = 0;

    int use_stdout = 0;
    int use_printf = 0;

    int need_cond = 0;

    char str[MAX_LEN];
    while (fgets(str, MAX_LEN, fp)) {
        int len = strlen(str);
        if (len <= 0) {
            read_errors++;
            continue;
        }
        if (len >= MAX_LEN - 1) {
            long_errors++;
        }
        if (str[len - 1] == '\r' || (len > 1 && str[len - 2] == '\r')) {
            crlf_errors++;
        }

        if (str[0] == '\t') {
            leading_tabs++;
        }
        if (len >= 4 && str[0] == ' ' && str[1] == ' ' && str[2] == ' ' && str[3] == ' ') {
            leading_spcs++;
        }

        if (strstr(str, "stdout")) {
            use_stdout++;
        }
        char *p;
        if ((p = strstr(str, "printf"))) {
            if (p == str || p[-1] < '_'|| p[-1] > 'z') {
                use_printf++;
            }
        }
        if (need_cond && !strstr(str, "if (!")) {
            // we had an alloc but no check on the following line
            memc_errors++;
        }
        need_cond = 0;
        if (strstr(str, "alloc(") && !strstr(str, "alloc()")) {
            need_cond++;
        }
        if (strstr(str, "strdup(") && !strstr(str, "strdup()")) {
            need_cond++;
        }
    }
    if (leading_tabs && leading_spcs) {
        tabs_errors = leading_tabs > leading_spcs ? leading_spcs : leading_tabs;
    }

    if (read_errors)
        printf("File \"%s\" has %d READ errors.\n", path, read_errors);
    if (long_errors)
        printf("File \"%s\" has %d LONG line errors.\n", path, long_errors);
    if (crlf_errors)
        printf("File \"%s\" has %d CRLF errors.\n", path, crlf_errors);
    if (tabs_errors)
        printf("File \"%s\" has %d MIXED tab/spaces errors.\n", path, tabs_errors);
    if (memc_errors)
        printf("File \"%s\" has %d ALLOC check errors.\n", path, memc_errors);
    if (leading_tabs)
        printf("File \"%s\" has %d TAB indented lines.\n", path, leading_tabs);
    if (strict && use_stdout)
        printf("File \"%s\" has %d STDOUT lines.\n", path, use_stdout);
    if (strict && use_printf)
        printf("File \"%s\" has %d PRINTF lines.\n", path, use_printf);

    return read_errors + long_errors + crlf_errors + tabs_errors + leading_tabs + (strict ? use_stdout + use_printf : 0) + memc_errors;
}

int main(int argc, char *argv[])
{
    int failed = 0;
    for (int i = 1; i < argc; ++i) {
        failed += style_check(argv[i]);
    }
    exit(!!failed);
}
