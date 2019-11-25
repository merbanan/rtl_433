/** @file
    Various utility functions for use by applications.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_R_UTIL_H_
#define INCLUDE_R_UTIL_H_

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "compat_time.h"

#if defined _MSC_VER // Microsoft Visual Studio
    // MSC has something like C99 restrict as __restrict
    #ifndef restrict
    #define restrict  __restrict
    #endif
#endif

// buffer to hold localized timestamp "YYYY-MM-DD HH:MM:SS.000000+0000"
#define LOCAL_TIME_BUFLEN 32

/** Get current time with usec precision.

    @param tv output for current time
*/
void get_time_now(struct timeval *tv);

/** Printable timestamp in local time.

    @param[out] buf output buffer, long enough for "YYYY-MM-DD HH:MM:SS+0000"
    @param format time format string, uses "%Y-%m-%d %H:%M:%S" if NULL
    @param with_tz 1 to add a time offset, 0 otherwise
    @param time_secs 0 for now, or seconds since the epoch
    @return buf pointer (for short hand use as operator)
*/
char *format_time_str(char *buf, char const *format, int with_tz, time_t time_secs);

/** Printable timestamp in local time with microseconds.

    @param[out] buf output buffer, long enough for "YYYY-MM-DD HH:MM:SS.uuuuuu+0000"
    @param format time format string without usec, uses "%Y-%m-%d %H:%M:%S" if NULL
    @param with_tz 1 to add a time offset, 0 otherwise
    @param tv NULL for now, or seconds and microseconds since the epoch
    @return buf pointer (for short hand use as operator)
*/
char *usecs_time_str(char *buf, char const *format, int with_tz, struct timeval *tv);

/** Printable sample position.

    @param sample_file_pos sample position
    @param buf output buffer, long enough for "@0.000000s"
    @return buf pointer (for short hand use as operator)
*/
char *sample_pos_str(float sample_file_pos, char *buf);

/** Convert Celsius to Fahrenheit.

    @param celsius temperature in Celsius
    @return temperature value in Fahrenheit
*/
float celsius2fahrenheit(float celsius);

/** Convert Fahrenheit to Celsius.

    @param fahrenheit temperature in Fahrenheit
    @return temperature value in Celsius
*/
float fahrenheit2celsius(float fahrenheit);

/** Convert Kilometers per hour (kph) to Miles per hour (mph).

    @param kph speed in Kilometers per hour
    @return speed in miles per hour
*/
float kmph2mph(float kph);

/** Convert Miles per hour (mph) to Kilometers per hour (kmph).

    @param mph speed in Kilometers per hour
    @return speed in kilometers per hour
*/
float mph2kmph(float mph);

/** Convert millimeters (mm) to inches (inch).

    @param mm measurement in millimeters
    @return measurement in inches
*/
float mm2inch(float mm);

/** Convert inches (inch) to millimeters (mm).

    @param inch measurement in inches
    @return measurement in millimeters
*/
float inch2mm(float inch);

/** Convert kilo Pascal (kPa) to pounds per square inch (PSI).

    @param kpa pressure in kPa
    @return pressure in PSI
*/
float kpa2psi(float kpa);

/** Convert pounds per square inch (PSI) to kilo Pascal (kPa).

    @param psi pressure in PSI
    @return pressure in kPa
*/
float psi2kpa(float psi);

/** Convert hecto Pascal (hPa) to inches of mercury (inHg).

    @param hpa pressure in kPa
    @return pressure in inHg
*/
float hpa2inhg(float hpa);

/** Convert inches of mercury (inHg) to hecto Pascal (hPa).

    @param inhg pressure in inHg
    @return pressure in hPa
*/
float inhg2hpa(float inhg);

/** Return true if the string ends with the specified suffix, otherwise return false.

    @param str string to search for patterns
    @param suffix the pattern to search
    @return true if the string ends with the specified suffix, false otherwise.
*/
bool str_endswith(char const *restrict str, char const *restrict suffix);

/** Replace a pattern in a string.

    This utility function is useful when converting native units to si or customary.

    @param orig string to search for patterns
    @param rep the pattern to replace
    @param with the replacement pattern
    @return a new string that has rep replaced with with
*/
char *str_replace(char const *orig, char const *rep, char const *with);

/** Make a nice printable string for a frequency.

    @param freq the frequency to convert to a string.
*/
char const *nice_freq (double freq);

#endif /* INCLUDE_R_UTIL_H_ */
