/** @file
    Various utility functions for use by applications

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "r_util.h"
#include "fatal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void get_time_now(struct timeval *tv)
{
    int ret = gettimeofday(tv, NULL);
    if (ret)
        perror("gettimeofday");
}

char *format_time_str(char *buf, char const *format, int with_tz, time_t time_secs)
{
    time_t etime;
    struct tm tm_info;

    if (time_secs == 0) {
        time(&etime);
    }
    else {
        etime = time_secs;
    }

#ifdef _WIN32 /* MinGW might have localtime_r but apparently not MinGW64 */
    localtime_s(&tm_info, &etime); // win32 doesn't have localtime_r()
#else
    localtime_r(&etime, &tm_info); // thread-safe
#endif

    if (!format || !*format)
        format = "%Y-%m-%d %H:%M:%S";

    size_t l = strftime(buf, LOCAL_TIME_BUFLEN, format, &tm_info);
    if (with_tz) {
        strftime(buf + l, LOCAL_TIME_BUFLEN - l, "%z", &tm_info);
        if (!strcmp(buf + l, "+0000"))
            strcpy(buf + l, "Z");
    }
    return buf;
}

char *usecs_time_str(char *buf, char const *format, int with_tz, struct timeval *tv)
{
    struct timeval now;
    struct tm tm_info;

    if (!tv) {
        tv = &now;
        get_time_now(tv);
    }

    time_t t_secs = tv->tv_sec;
#ifdef _WIN32 /* MinGW might have localtime_r but apparently not MinGW64 */
    localtime_s(&tm_info, &t_secs); // win32 doesn't have localtime_r()
#else
    localtime_r(&t_secs, &tm_info); // thread-safe
#endif

    if (!format || !*format)
        format = "%Y-%m-%d %H:%M:%S";

    size_t l = strftime(buf, LOCAL_TIME_BUFLEN, format, &tm_info);
    l += snprintf(buf + l, LOCAL_TIME_BUFLEN - l, ".%06ld", (long)tv->tv_usec);
    if (with_tz) {
        strftime(buf + l, LOCAL_TIME_BUFLEN - l, "%z", &tm_info);
        if (!strcmp(buf + l, "+0000"))
            strcpy(buf + l, "Z");
    }
    return buf;
}

char *sample_pos_str(float sample_file_pos, char *buf)
{
    snprintf(buf, LOCAL_TIME_BUFLEN, "@%fs", sample_file_pos);
    return buf;
}

float celsius2fahrenheit(float celsius)
{
  return celsius * (9.0f / 5.0f) + 32;
}


float fahrenheit2celsius(float fahrenheit)
{
    return (fahrenheit - 32) * (5.0f / 9.0f);
}


float kmph2mph(float kmph)
{
    return kmph * (1.0f / 1.609344f);
}

float mph2kmph(float mph)
{
    return mph * 1.609344f;
}


float mm2inch(float mm)
{
    return mm * 0.039370f;
}

float inch2mm(float inch)
{
    return inch * 25.4f;
}


float kpa2psi(float kpa)
{
    return kpa * (1.0f / 6.89475729f);
}

float psi2kpa(float psi)
{
    return psi * 6.89475729f;
}


float hpa2inhg(float hpa)
{
    return hpa * (1.0f / 33.8639f);
}

float inhg2hpa(float inhg)
{
    return inhg * 33.8639f;
}


bool str_endswith(char const *restrict str, char const *restrict suffix)
{
    int str_len = strlen(str);
    int suffix_len = strlen(suffix);

    return (str_len >= suffix_len) &&
           (0 == strcmp(str + (str_len - suffix_len), suffix));
}

// Original string replacement function was found here:
// https://stackoverflow.com/questions/779875/what-is-the-function-to-replace-string-in-c/779960#779960
//
// You must free the result if result is non-NULL.
static char *new_str_replace(char const *orig, char const *rep, char const *with)
{
    char *result;  // the return string
    char const *ins; // the next insert point
    char *tmp;     // varies
    int len_rep;   // length of rep (the string to remove)
    int len_with;  // length of with (the string to replace rep with)
    int len_front; // distance between rep and end of last rep
    int count;     // number of replacements

    // sanity checks and initialization
    if (!orig || !rep)
        return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0)
        return NULL; // empty rep causes infinite loop during count
    if (!with)
        with = "";
    len_with = strlen(with);

    // count the number of replacements needed
    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * (size_t)count + 1);
    if (!result) {
        WARN_MALLOC("str_replace()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }

    // first time through the loop, all the variables are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of rep in orig
    //    orig points to the remainder of orig after "end of rep"
    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig);
    return result;
}

struct replace_map {
    char const *src;
    char *dst;
    char const *rep;
    char const *with;
};

#define REPLACE_MAP_SIZE 100
static struct replace_map replace_map[REPLACE_MAP_SIZE];

/*
    "temperature_C", "_C", "_F"
    "%.1f C", "C", "F"
    "%.01f C", "C", "F"
    "pressure_kPa", "_kPa", "_PSI"
    "%.1f kPa", "kPa", "PSI"
    "%.0f C", "C", "F"
    "temperature_1_C", "_C", "_F"
    "wind_avg_km_h", "_km_h", "_mi_h"
    "%.1f km/h", "km/h", "mi/h"
    "%.02f C", "C", "F"
    "rain_mm", "_mm", "_in"
    "%.02f mm", "mm", "in"
    "%.1f mm", "mm", "in"
    "%.1f", "C", "F"
    "%.2f C", "C", "F"
    "setpoint_C", "_C", "_F"
    "pressure_hPa", "_hPa", "_inHg"
    "%.01f hPa", "hPa", "inHg"
    "%.02f", "km/h", "mi/h"
    "wind_max_km_h", "_km_h", "_mi_h"
    "%3.1f", "mm", "in"
    "%.01f mm", "mm", "in"
    "%3.2f mm", "mm", "in"
    "temperature_2_C", "_C", "_F"
    "%.0f hPa", "hPa", "inHg"
*/

char *str_replace(char const *orig, char const *rep, char const *with)
{
    if (!orig)
        return NULL;

    struct replace_map *p = replace_map;
    for (; p->src; ++p) {
        if (!strcmp(p->src, orig)) {
            if (p->rep != rep || p->with != with) {
                fprintf(stderr, "%s: mismatch in \"%s\", \"%s\", \"%s\"\n", __func__, orig, rep, with);
            }
            return p->dst;
        }
    }
    // not found: replace and append
    fprintf(stderr, "%s: appending \"%s\", \"%s\", \"%s\"\n", __func__, orig, rep, with);
    char *dst = new_str_replace(orig, rep, with);
    p->src    = orig;
    p->dst    = dst;
    p->rep    = rep;
    p->with   = with;
    return dst;
}

// Make a more readable string for a frequency.
char const *nice_freq (double freq)
{
  static char buf[30];

  if (freq >= 1E9)
     snprintf (buf, sizeof(buf), "%.3fGHz", freq/1E9);
  else if (freq >= 1E6)
     snprintf (buf, sizeof(buf), "%.3fMHz", freq/1E6);
  else if (freq >= 1E3)
     snprintf (buf, sizeof(buf), "%.3fkHz", freq/1E3);
  else
     snprintf (buf, sizeof(buf), "%f", freq);
  return (buf);
}
