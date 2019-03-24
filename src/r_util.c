/** @file
    Various utility functions for use by applications

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "r_util.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void get_time_now(struct timeval *tv)
{
    int ret = gettimeofday(tv, NULL);
    if (ret)
        perror("gettimeofday");
}

char *local_time_str(time_t time_secs, char *buf)
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

    strftime(buf, LOCAL_TIME_BUFLEN, "%Y-%m-%d %H:%M:%S", &tm_info);
    return buf;
}

char *usecs_time_str(struct timeval *tv, char *buf)
{
    struct timeval now;
    struct tm *tm_info;

    if (!tv) {
        tv = &now;
        get_time_now(tv);
    }

    time_t t_secs = tv->tv_sec;
    tm_info = localtime(&t_secs); // note: win32 doesn't have localtime_r()

    size_t l = strftime(buf, LOCAL_TIME_BUFLEN, "%Y-%m-%d %H:%M:%S", tm_info);
    snprintf(buf + l, LOCAL_TIME_BUFLEN - l, ".%06ld", (long)tv->tv_usec);
    return buf;
}

char *sample_pos_str(float sample_file_pos, char *buf)
{
    snprintf(buf, LOCAL_TIME_BUFLEN, "@%fs", sample_file_pos);
    return buf;
}

float celsius2fahrenheit(float celsius)
{
  return celsius * 9 / 5 + 32;
}


float fahrenheit2celsius(float fahrenheit)
{
    return (fahrenheit - 32) / 1.8;
}


float kmph2mph(float kmph)
{
    return kmph / 1.609344;
}

float mph2kmph(float mph)
{
    return mph * 1.609344;
}


float mm2inch(float mm)
{
    return mm * 0.039370;
}

float inch2mm(float inch)
{
    return inch / 0.039370;
}


float kpa2psi(float kpa)
{
    return kpa / 6.89475729;
}

float psi2kpa(float psi)
{
    return psi * 6.89475729;
}


float hpa2inhg(float hpa)
{
    return hpa / 33.8639;
}

float inhg2hpa(float inhg)
{
    return inhg * 33.8639;
}


bool str_endswith(const char *restrict str, const char *restrict suffix)
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
char *str_replace(char *orig, char *rep, char *with)
{
    char *result;  // the return string
    char *ins;     // the next insert point
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

    tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

    if (!result)
        return NULL;

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

// Make a more readable string for a frequency.
const char *nice_freq (double freq)
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
