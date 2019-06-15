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
    FILE *fp = fopen(path, "r");
    assert(fp);

    int read_errors = 0;
    int long_errors = 0;
    int crlf_errors = 0;
    int tabs_errors = 0;

    int leading_tabs = 0;
    int leading_spcs = 0;

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

    return read_errors + long_errors + crlf_errors + tabs_errors;
}

int main(int argc, char *argv[])
{
    int failed = 0;
    for (int i = 1; i < argc; ++i) {
        failed += style_check(argv[i]);
    }
    exit(!!failed);
}
