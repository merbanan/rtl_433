/** @file
    Various utility functions handling file formats.

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_FILEFORMAT_H_
#define INCLUDE_FILEFORMAT_H_

#include <stdint.h>
#include <stdio.h>

char const *file_basename(char const *path);

/// a single handy number to define the file type.
/// bitmask: RRRR LLLL WWWWWWWW 00CC 00FS
enum file_type {
    // format bits
    F_UNSIGNED = 0 << 0,
    F_SIGNED   = 1 << 0,
    F_INT      = 0 << 1,
    F_FLOAT    = 1 << 1,
    F_1CH      = 1 << 4,
    F_2CH      = 2 << 4,
    F_W8       = 8 << 8,
    F_W12      = 12 << 8,
    F_W16      = 16 << 8,
    F_W32      = 32 << 8,
    F_W64      = 64 << 8,
    // content types
    F_I        = 1 << 16,
    F_Q        = 2 << 16,
    F_AM       = 3 << 16,
    F_FM       = 4 << 16,
    F_IQ       = F_I | F_Q << 4,
    F_LOGIC    = 5 << 16,
    F_VCD      = 6 << 16,
    F_OOK      = 7 << 16,
    // format types
    F_U8       = F_1CH | F_UNSIGNED | F_INT | F_W8,
    F_S8       = F_1CH | F_SIGNED   | F_INT | F_W8,
    F_CU8      = F_2CH | F_UNSIGNED | F_INT | F_W8,
    F_CS8      = F_2CH | F_SIGNED   | F_INT | F_W8,
    F_U16      = F_1CH | F_UNSIGNED | F_INT | F_W16,
    F_S16      = F_1CH | F_SIGNED   | F_INT | F_W16,
    F_CU16     = F_2CH | F_UNSIGNED | F_INT | F_W16,
    F_CS16     = F_2CH | F_SIGNED   | F_INT | F_W16,
    F_U32      = F_1CH | F_UNSIGNED | F_INT | F_W32,
    F_S32      = F_1CH | F_SIGNED   | F_INT | F_W32,
    F_CU32     = F_2CH | F_UNSIGNED | F_INT | F_W32,
    F_CS32     = F_2CH | F_SIGNED   | F_INT | F_W32,
    F_F32      = F_1CH | F_SIGNED   | F_FLOAT | F_W32,
    F_CF32     = F_2CH | F_SIGNED   | F_FLOAT | F_W32,
    // compound types
    CU8_IQ     = F_CU8 | F_IQ,
    CS8_IQ     = F_CS8 | F_IQ,
    S16_AM     = F_S16 | F_AM,
    S16_FM     = F_S16 | F_FM,
    CS16_IQ    = F_CS16 | F_IQ,
    CF32_IQ    = F_CF32 | F_IQ,
    F32_AM     = F_F32 | F_AM,
    F32_FM     = F_F32 | F_FM,
    F32_I      = F_F32 | F_I,
    F32_Q      = F_F32 | F_Q,
    U8_LOGIC   = F_LOGIC | F_U8,
    VCD_LOGIC  = F_VCD,
    PULSE_OOK  = F_OOK,
};

typedef struct {
    uint32_t format;
    uint32_t raw_format;
    uint32_t center_frequency;
    uint32_t sample_rate;
    char const *spec;
    char const *path;
    FILE *file;
} file_info_t;

/// Clear all file info.
///
/// @param[in,out] info the file info to clear
void file_info_clear(file_info_t *info);

/// Parse file info from a filename, optionally prefixed with overrides.
///
/// Detects tags in the file name delimited by non-alphanum
/// and prefixes delimited with a colon.
///
/// Parse "[0-9]+(\.[0-9]+)?[A-Za-z]"
/// - as frequency (suffix "M" or "[kMG]?Hz")
/// - or sample rate (suffix "k" or "[kMG]?sps")
///
/// Parse "[A-Za-z][0-9A-Za-z]+" as format or content specifier:
/// - 2ch formats: "cu8", "cs8", "cs16", "cs32", "cf32"
/// - 1ch formats: "u8", "s8", "s16", "u16", "s32", "u32", "f32"
/// - text formats: "vcd", "ook"
/// - content types: "iq", "i", "q", "am", "fm", "logic"
///
/// Parses left to right, with the exception of a prefix up to the last colon ":"
/// This prefix is the forced override, parsed last and removed from the filename.
///
/// All matches are case-insensitive.
///
/// - default detection, e.g.: path/filename.am.s16
/// - overrides, e.g.: am:s16:path/filename.ext
/// - other styles are detected but discouraged, e.g.:
///   am-s16:path/filename.ext, am.s16:path/filename.ext, path/filename.am_s16
///
/// @param[in,out] info the file info to parse into
/// @param filename a file name with optional override prefix to parse
/// @return the detected file format, 0 otherwise
int file_info_parse_filename(file_info_t *info, const char *filename);

/// Check if the format in this file info is supported for reading,
/// print a warning and exit otherwise.
///
/// @param info the file info to check
void file_info_check_read(file_info_t *info);

/// Check if the format in this file info is supported for reading,
/// print a warning and exit otherwise.
///
/// @param info the file info to check
void file_info_check_write(file_info_t *info);

/// Return a string describing the format in this file info.
///
/// @param info the file info to check
/// @return a string describing the format
char const *file_info_string(file_info_t *info);

#endif /* INCLUDE_FILEFORMAT_H_ */
