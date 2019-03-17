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

int parse_file_info(const char *filename, file_info_t *info);

void check_read_file_info(file_info_t *info);

void check_write_file_info(file_info_t *info);

char const *file_info_string(file_info_t *info);

#endif /* INCLUDE_FILEFORMAT_H_ */
