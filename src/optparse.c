/**
 * Option parsing functions to complement getopt.
 *
 * Copyright (C) 2017 Christian Zuckschwerdt
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "optparse.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

uint32_t atouint32_metric(const char *str, const char *error_hint)
{
    if (!str) {
        fprintf(stderr, "%smissing number argument\n", error_hint);
        exit(1);
    }

    if (!*str) {
        fprintf(stderr, "%sempty number argument\n", error_hint);
        exit(1);
    }

    char *endptr;
    double val = strtod(str, &endptr);

    if (str == endptr) {
        fprintf(stderr, "%sinvalid number argument (%s)\n", error_hint, str);
        exit(1);
    }

    if (val < 0.0) {
        fprintf(stderr, "%snon-negative number argument expected (%f)\n", error_hint, val);
        exit(1);
    }

    // allow whitespace before suffix
    while (*endptr == ' ' || *endptr == '\t')
        ++endptr;

    switch (*endptr) {
        case '\0':
            break;
        case 'k':
        case 'K':
            val *= 1e3;
            break;
        case 'M':
        case 'm':
            val *= 1e6;
            break;
        case 'G':
        case 'g':
            val *= 1e9;
            break;
        default:
            fprintf(stderr, "%sunknown number suffix (%s)\n", error_hint, endptr);
            exit(1);
    }

    if (val > UINT32_MAX) {
        fprintf(stderr, "%snumber argument too big (%f)\n", error_hint, val);
        exit(1);
    }

    if ((uint32_t)((val - (uint32_t)val) * 1e6) != 0) {
        fprintf(stderr, "%sdecimal fraction (%f) did you forget k, M, or G suffix?\n", error_hint, val - (uint32_t)val);
    }

    return (uint32_t)val;
}

int atoi_time(const char *str, const char *error_hint)
{
    if (!str) {
        fprintf(stderr, "%smissing time argument\n", error_hint);
        exit(1);
    }

    if (!*str) {
        fprintf(stderr, "%sempty time argument\n", error_hint);
        exit(1);
    }

    char *endptr;
    double val = strtod(str, &endptr);

    if (str == endptr) {
        fprintf(stderr, "%sinvalid time argument (%s)\n", error_hint, str);
        exit(1);
    }

    // allow whitespace before suffix
    while (*endptr == ' ' || *endptr == '\t')
        ++endptr;

    switch (*endptr) {
        case '\0':
            break;
        case 's':
        case 'S':
            break;
        case 'm':
        case 'M':
            val *= 60;
            break;
        case 'h':
        case 'H':
            val *= 60 * 60;
            break;
        default:
            fprintf(stderr, "%sunknown time suffix (%s)\n", error_hint, endptr);
            exit(1);
    }

    if (val > INT_MAX || val < INT_MIN) {
        fprintf(stderr, "%stime argument too big (%f)\n", error_hint, val);
        exit(1);
    }

    if ((uint32_t)((val - (uint32_t)val) * 1e6) != 0) {
        fprintf(stderr, "%sdecimal fraction (%f) did you forget m, or h suffix?\n", error_hint, val - (uint32_t)val);
    }

    return (int)val;
}

char *asepc(char **stringp, char delim)
{
    if (!stringp || !*stringp) return NULL;
    char *s = strchr(*stringp, delim);
    if (s) *s++ = '\0';
    char *p = *stringp;
    *stringp = s;
    return p;
}

char *getkwargs(char **s, char **key, char **val)
{
    char *v = asepc(s, ',');
    char *k = asepc(&v, '=');
    if (key) *key = k;
    if (val) *val = v;
    return k;
}

// Test code
// gcc -I include/ -std=c99 -D _TEST src/optparse.c && ./a.out
#ifdef _TEST
#define ASSERT_EQUALS(a,b) if ((a) == (b)) { ++passed; } else { ++failed; fprintf(stderr, "FAIL: %d <> %d\n", (a), (b)); }
int main(int argc, char **argv)
{
    unsigned passed = 0;
    unsigned failed = 0;

    fprintf(stderr, "optparse:: atouint32_metric\n");
    ASSERT_EQUALS(atouint32_metric("0", ""), 0);
    ASSERT_EQUALS(atouint32_metric("1", ""), 1);
    ASSERT_EQUALS(atouint32_metric("0.0", ""), 0);
    ASSERT_EQUALS(atouint32_metric("1.0", ""), 1);
    ASSERT_EQUALS(atouint32_metric("1.024k", ""), 1024);
    ASSERT_EQUALS(atouint32_metric("433.92MHz", ""), 433920000);
    ASSERT_EQUALS(atouint32_metric(" +1 G ", ""), 1000000000);

    fprintf(stderr, "optparse:: atoi_time\n");
    ASSERT_EQUALS(atoi_time("0", ""), 0);
    ASSERT_EQUALS(atoi_time("1", ""), 1);
    ASSERT_EQUALS(atoi_time("0.0", ""), 0);
    ASSERT_EQUALS(atoi_time("1.0", ""), 1);
    ASSERT_EQUALS(atoi_time("1s", ""), 1);
    ASSERT_EQUALS(atoi_time("2h", ""), 2*60*60);
    ASSERT_EQUALS(atoi_time(" -1 M ", ""), -60);

    fprintf(stderr, "optparse:: test (%u/%u) passed, (%u) failed.\n", passed, passed + failed, failed);

    return failed;
}
#endif /* _TEST */
