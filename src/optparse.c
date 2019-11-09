/** @file
    Option parsing functions to complement getopt.

    Copyright (C) 2017 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "optparse.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

int atobv(char *arg, int def)
{
    if (!arg)
        return def;
    if (!strcasecmp(arg, "true") || !strcasecmp(arg, "yes") || !strcasecmp(arg, "on") || !strcasecmp(arg, "enable"))
        return 1;
    return atoi(arg);
}

int atoiv(char *arg, int def)
{
    if (!arg)
        return def;
    char *endptr;
    int val = strtol(arg, &endptr, 10);
    if (arg == endptr)
        return def;
    return val;
}

char *arg_param(char *arg)
{
    if (!arg)
        return NULL;
    char *p = strchr(arg, ':');
    char *c = strchr(arg, ',');
    if (p && (!c || p < c))
        return ++p;
    else if (c)
        return c;
    else
        return p;
}

char *hostport_param(char *param, char **host, char **port)
{
    if (param && *param) {
        if (param[0] == '/' && param[1] == '/') {
            param += 2;
        }
        if (*param != ':' && *param != ',') {
            *host = param;
            if (*param == '[') {
                (*host)++;
                param = strchr(param, ']');
                if (param) {
                    *param++ = '\0';
                }
                else {
                    fprintf(stderr, "Malformed Ipv6 address!\n");
                    exit(1);
                }
            }
        }
        char *colon = strchr(param, ':');
        char *comma = strchr(param, ',');
        if (colon && (!comma || colon < comma)) {
            *colon++ = '\0';
            *port    = colon;
        }
        if (comma) {
            *comma++ = '\0';
            return comma;
        }
    }
    return NULL;
}

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

    char *endptr    = NULL;
    double val      = 0.0;
    unsigned colons = 0;

    do {
        double num = strtod(str, &endptr);

        if (!endptr || str == endptr) {
            fprintf(stderr, "%sinvalid time argument (%s)\n", error_hint, str);
            exit(1);
        }

        // allow whitespace before suffix
        while (*endptr == ' ' || *endptr == '\t')
            ++endptr;

        switch (*endptr) {
        case '\0':
            if (colons == 0) {
                // assume seconds
                val += num;
                break;
            }
            // intentional fallthrough
        case ':':
            ++colons;
            if (colons == 1)
                val += num * 60 * 60;
            else if (colons == 2)
                val += num * 60;
            else if (colons == 3)
                val += num;
            else {
                fprintf(stderr, "%stoo many colons (use HH:MM[:SS]))\n", error_hint);
                exit(1);
            }
            if (*endptr)
                ++endptr;
            break;
        case 's':
        case 'S':
            val += num;
            ++endptr;
            break;
        case 'm':
        case 'M':
            val += num * 60;
            ++endptr;
            break;
        case 'h':
        case 'H':
            val += num * 60 * 60;
            ++endptr;
            break;
        case 'd':
        case 'D':
            val += num * 60 * 60 * 24;
            ++endptr;
            break;
        default:
            fprintf(stderr, "%sunknown time suffix (%s)\n", error_hint, endptr);
            exit(1);
        }

        // chew up any remaining whitespace
        while (*endptr == ' ' || *endptr == '\t')
            ++endptr;
        str = endptr;

    } while (*endptr);

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

char *trim_ws(char *str)
{
    if (!str || !*str)
        return str;
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n')
        ++str;
    char *e = str; // end pointer (last non ws)
    char *p = str; // scanning pointer
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            ++p;
        if (*p)
            e = p++;
    }
    *++e = '\0';
    return str;
}

char *remove_ws(char *str)
{
    if (!str)
        return str;
    char *d = str; // dst pointer
    char *s = str; // src pointer
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
            ++s;
        if (*s)
            *d++ = *s++;
    }
    *d++ = '\0';
    return str;
}

// Unit testing
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
    ASSERT_EQUALS(atoi_time("2d", ""), 2 * 60 * 60 * 24);
    ASSERT_EQUALS(atoi_time("2h", ""), 2 * 60 * 60);
    ASSERT_EQUALS(atoi_time("2m", ""), 2 * 60);
    ASSERT_EQUALS(atoi_time("2s", ""), 2);
    ASSERT_EQUALS(atoi_time("2D", ""), 2 * 60 * 60 * 24);
    ASSERT_EQUALS(atoi_time("2H", ""), 2 * 60 * 60);
    ASSERT_EQUALS(atoi_time("2M", ""), 2 * 60);
    ASSERT_EQUALS(atoi_time("2S", ""), 2);
    ASSERT_EQUALS(atoi_time("2h3m4s", ""), 2 * 60 * 60 + 3 * 60 + 4);
    ASSERT_EQUALS(atoi_time("2h 3m 4s", ""), 2 * 60 * 60 + 3 * 60 + 4);
    ASSERT_EQUALS(atoi_time("2h3h 3m 4s 5", ""), 5 * 60 * 60 + 3 * 60 + 9);
    ASSERT_EQUALS(atoi_time(" 2m ", ""), 2 * 60);
    ASSERT_EQUALS(atoi_time("2 m", ""), 2 * 60);
    ASSERT_EQUALS(atoi_time("  2  m  ", ""), 2 * 60);
    ASSERT_EQUALS(atoi_time("-1m", ""), -60);
    ASSERT_EQUALS(atoi_time("1h-15m", ""), 45 * 60);

    ASSERT_EQUALS(atoi_time("2:3", ""), 2 * 60 * 60 + 3 * 60);
    ASSERT_EQUALS(atoi_time("2:3:4", ""), 2 * 60 * 60 + 3 * 60 + 4);
    ASSERT_EQUALS(atoi_time("02:03", ""), 2 * 60 * 60 + 3 * 60);
    ASSERT_EQUALS(atoi_time("02:03:04", ""), 2 * 60 * 60 + 3 * 60 + 4);
    ASSERT_EQUALS(atoi_time(" 2 : 3 ", ""), 2 * 60 * 60 + 3 * 60);
    ASSERT_EQUALS(atoi_time(" 2 : 3 : 4 ", ""), 2 * 60 * 60 + 3 * 60 + 4);

    fprintf(stderr, "optparse:: test (%u/%u) passed, (%u) failed.\n", passed, passed + failed, failed);

    return failed;
}
#endif /* _TEST */
