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
            strcpy(buf + l, "Z"); // NOLINT
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
            strcpy(buf + l, "Z"); // NOLINT
    }
    return buf;
}

const char *parse_time_str(const char *buf, struct timeval *out)
{
    int year, mon, day, hour, min, sec, consumed;
    long micros = 0;

    if (sscanf(buf, "%4d-%2d-%2d%*1[ T]%2d:%2d:%2d%n", &year, &mon, &day, &hour, &min, &sec, &consumed) == 6) {
        buf += consumed;
    }
    else {
        return NULL;
    }

    if (*buf == '.') {
        long digit_micros = 100000;
        buf++;
        while (*buf >= '0' && *buf <= '9') {
            micros += (*buf - '0') * digit_micros;
            digit_micros /= 10;
            buf++;
        }
    }

    int gmtoff = 0;
    if (*buf == '+' || *buf == '-') {
        int tz_hours = 0, tz_mins = 0, tz_sign = 1;
        if (*buf == '-')
            tz_sign = -1;
        buf += 1;
        if (sscanf(buf, "%2d%2d%n", &tz_hours, &tz_mins, &consumed) == 2) {
            buf += consumed;
            gmtoff = tz_sign * (tz_hours * 3600 + tz_mins * 60);
        }
        else {
            return NULL;
        }
    }
    else if (*buf == 'Z') {
        buf++;
    }
    else if (*buf != '\0') {
        return NULL;
    }

    struct tm tm = {0};
    tm.tm_year   = year - 1900;
    tm.tm_mon    = mon - 1;
    tm.tm_mday   = day;
    tm.tm_hour   = hour;
    tm.tm_min    = min;
    tm.tm_sec    = sec;
    tm.tm_isdst  = -1;

    time_t epoch_sec = timegm(&tm);
    if (epoch_sec == -1) {
        return NULL;
    }
    out->tv_sec  = epoch_sec - gmtoff;
    out->tv_usec = micros;

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
    if (!suffix) {
        return true;
    }
    if (!str) {
        return false;
    }
    int str_len = strlen(str);
    int suffix_len = strlen(suffix);

    return (str_len >= suffix_len) &&
           (0 == strcmp(str + (str_len - suffix_len), suffix));
}

// Original string replacement function was found here:
// https://stackoverflow.com/questions/779875/what-is-the-function-to-replace-string-in-c/779960#779960
//
// You must free the result if result is non-NULL.
char *str_replace(char const *orig, char const *rep, char const *with)
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
        tmp = strcpy(tmp, with) + len_with; // NOLINT
        orig += len_front + len_rep; // move to next "end of rep"
    }
    strcpy(tmp, orig); // NOLINT
    return result;
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

#ifdef _TEST
#define TEST_PARSE_TIME_STR(str, epoch, millis) \
    do { \
        struct timeval tv; \
        if (parse_time_str(str, &tv) != NULL) { \
            if (tv.tv_sec == (epoch) && tv.tv_usec == (millis)) { \
                ++passed; \
            } \
            else { \
                ++failed; \
                fprintf(stderr, "FAIL: parse_time_str \"%s\" = %ld.%06ld, expected %ld.%06d\n", str, tv.tv_sec, (long)tv.tv_usec, (long)(epoch), (millis)); \
            } \
        } \
        else { \
            ++failed; \
            fprintf(stderr, "FAIL: parse_time_str failed to parse \"%s\"\n", str); \
        } \
    } while (0)

int main(void)
{
    unsigned passed = 0;
    unsigned failed = 0;

    TEST_PARSE_TIME_STR("2026-02-11T12:34:56.123456Z", 1770813296, 123456);
    TEST_PARSE_TIME_STR("2026-02-11 12:34:56.123456Z", 1770813296, 123456);
    TEST_PARSE_TIME_STR("2026-02-11 12:34:56.123Z", 1770813296, 123000);
    TEST_PARSE_TIME_STR("2026-02-11 12:34:56.111111111Z", 1770813296, 111111);
    TEST_PARSE_TIME_STR("2026-02-11 12:34:56Z", 1770813296, 0);
    TEST_PARSE_TIME_STR("2026-02-11 12:34:56.123456-0700", 1770838496, 123456);
    TEST_PARSE_TIME_STR("2026-02-11 12:34:56.123456+0845", 1770781796, 123456);

    fprintf(stderr, "r_util test (%u/%u) passed, (%u) failed.\n", passed, passed + failed, failed);
    return failed;
}
#endif /* _TEST */
