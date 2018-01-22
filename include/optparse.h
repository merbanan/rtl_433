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

#ifndef OPTPARSE_H_
#define OPTPARSE_H_

#include <stdint.h>

/// Convert a string to an unsigned integer, uses strtod() and accepts
/// metric suffixes of 'k', 'M', and 'G' (also 'K', 'm', and 'g').
///
/// Parse errors will fprintf(stderr, ...) and exit(1).
///
/// @param str: character string to parse
/// @param error_hint: prepended to error output
/// @return parsed number value
uint32_t atouint32_metric(const char *str, const char *error_hint);

/// Convert a string to an integer, uses strtod() and accepts
/// time suffixes of 's', 'm', and 'h' (also 'S', 'M', and 'H').
///
/// Parse errors will fprintf(stderr, ...) and exit(1).
///
/// @param str: character string to parse
/// @param error_hint: prepended to error output
/// @return parsed number value
int atoi_time(const char *str, const char *error_hint);

/// Similar to strsep.
///
/// @param[in,out] stringp
/// @param delim the delimiter character
/// @return the original value of *stringp
char *asepc(char **stringp, char delim);

/// Parse a comma-separated list of key/value pairs into kwargs
///
/// The input string will be modified and the pointer advanced.
/// The key and val pointers will be into the original string.
///
/// @param[in,out] s String of key=value pairs, separated by commas
/// @param[out] key keyword argument if found, NULL otherwise
/// @param[out] val value if found, NULL otherwise
/// @return the original value of *stringp (the keyword found)
char *getkwargs(char **s, char **key, char **val);

#endif /* OPTPARSE_H_ */
