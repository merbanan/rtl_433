/** @file
    Various utility functions handling file formats.

    Copyright (C) 2018 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <string.h>
#include <stdlib.h>
#ifdef _MSC_VER
#ifndef strncasecmp // Microsoft Visual Studio
#define strncasecmp  _strnicmp
#endif
#else
#include <strings.h>
#endif
//#include "optparse.h"
#include "fileformat.h"

#ifdef _WIN32
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif

char const *file_basename(char const *path)
{
    char const *p = strrchr(path, PATH_SEPARATOR);
    if (p)
        return p + 1;
    else
        return path;
}

void check_read_file_info(file_info_t *info)
{
    if (info->format != CU8_IQ
            && info->format != CS16_IQ
            && info->format != CF32_IQ
            && info->format != S16_AM
            && info->format != PULSE_OOK) {
        fprintf(stderr, "File type not supported as input (%s).\n", info->spec);
        exit(1);
    }
}

void check_write_file_info(file_info_t *info)
{
    if (info->format != CU8_IQ
            && info->format != CS8_IQ
            && info->format != S16_AM
            && info->format != S16_FM
            && info->format != CS16_IQ
            && info->format != CF32_IQ
            && info->format != F32_AM
            && info->format != F32_FM
            && info->format != F32_I
            && info->format != F32_Q
            && info->format != U8_LOGIC
            && info->format != VCD_LOGIC) {
        fprintf(stderr, "File type not supported as output (%s).\n", info->spec);
        exit(1);
    }
}

char const *file_info_string(file_info_t *info)
{
    switch (info->format) {
    case CU8_IQ:    return "CU8 IQ (2ch uint8)"; break;
    case S16_AM:    return "S16 AM (1ch int16)"; break;
    case S16_FM:    return "S16 FM (1ch int16)"; break;
    case CF32_IQ:   return "CF32 IQ (2ch float32)"; break;
    case CS16_IQ:   return "CS16 IQ (2ch int16)"; break;
    case F32_AM:    return "F32 AM (1ch float32)"; break;
    case F32_FM:    return "F32 FM (1ch float32)"; break;
    case F32_I:     return "F32 I (1ch float32)"; break;
    case F32_Q:     return "F32 Q (1ch float32)"; break;
    case VCD_LOGIC: return "VCD logic (text)"; break;
    case U8_LOGIC:  return "U8 logic (1ch uint8)"; break;
    case PULSE_OOK: return "OOK pulse data (text)"; break;
    default:        return "Unknown";  break;
    }
}

static void file_type_set_format(uint32_t *type, uint32_t val)
{
    *type = (*type & 0xffff0000) | val;
}

static void file_type_set_content(uint32_t *type, uint32_t val)
{
    *type = (*type & 0x0000ffff) | val;
}

static uint32_t file_type_guess_auto_format(uint32_t type)
{
    if (type == 0) return CU8_IQ;
    else if (type == F_IQ) return CU8_IQ;
    else if (type == F_AM) return S16_AM;
    else if (type == F_FM) return S16_FM;
    else if (type == F_I) return F32_I;
    else if (type == F_Q) return F32_Q;
    else if (type == F_LOGIC) return U8_LOGIC;

    else if (type == F_CU8) return CU8_IQ;
    else if (type == F_CS8) return CS8_IQ;
    else if (type == F_S16) return S16_AM;
    else if (type == F_U8) return U8_LOGIC;
    else if (type == F_Q) return F32_Q;
    else if (type == F_VCD) return VCD_LOGIC;
    else if (type == F_OOK) return PULSE_OOK;
    else if (type == F_CS16) return CS16_IQ;
    else if (type == F_CF32) return CF32_IQ;
    else return type;
}

static void file_type(char const *filename, file_info_t *info)
{
    if (!filename || !*filename) {
        return;
    }

    char const *p = filename;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            char const *n = p; // number starts here
            while (*p >= '0' && *p <= '9')
                ++p;
            if (*p == '.') {
                ++p;
                // if not [0-9] after '.' abort
                if (*p < '0' || *p > '9')
                    continue;
                while (*p >= '0' && *p <= '9')
                    ++p;
            }
            char const *s = p; // number ends and unit starts here
            while ((*p >= 'A' && *p <= 'Z')
                    || (*p >= 'a' && *p <= 'z'))
                ++p;
            double num = atof(n); // atouint32_metric() ?
            size_t len = p - s;
            double scale = 1.0;
            switch (*s) {
            case 'k':
            case 'K':
                scale *= 1e3;
                break;
            case 'M':
            case 'm':
                scale *= 1e6;
                break;
            case 'G':
            case 'g':
                scale *= 1e9;
                break;
            }
            if (len == 1 && !strncasecmp("M", s, 1)) info->center_frequency = num * 1e6;
            else if (len == 1 && !strncasecmp("k", s, 1)) info->sample_rate = num * 1e3;
            else if (len == 2 && !strncasecmp("Hz", s, 2)) info->center_frequency = num;
            else if (len == 3 && !strncasecmp("sps", s, 3)) info->sample_rate = num;
            else if (len == 3 && !strncasecmp("Hz", s+1, 2) && scale > 1.0) info->center_frequency = num * scale;
            else if (len == 4 && !strncasecmp("sps", s+1, 3) && scale > 1.0) info->sample_rate = num * scale;
            //fprintf(stderr, "Got number %g, f is %u, s is %u\n", num, info->center_frequency, info->sample_rate);
        } else if ((*p >= 'A' && *p <= 'Z')
                || (*p >= 'a' && *p <= 'z')) {
            char const *t = p; // type starts here
            while ((*p >= '0' && *p <= '9')
                    || (*p >= 'A' && *p <= 'Z')
                    || (*p >= 'a' && *p <= 'z'))
                ++p;
            size_t len = p - t;
            if (len == 1 && !strncasecmp("i", t, 1)) file_type_set_content(&info->format, F_I);
            else if (len == 1 && !strncasecmp("q", t, 1)) file_type_set_content(&info->format, F_Q);
            else if (len == 2 && !strncasecmp("iq", t, 2)) file_type_set_content(&info->format, F_IQ);
            else if (len == 2 && !strncasecmp("am", t, 2)) file_type_set_content(&info->format, F_AM);
            else if (len == 2 && !strncasecmp("fm", t, 2)) file_type_set_content(&info->format, F_FM);
            else if (len == 2 && !strncasecmp("u8", t, 2)) file_type_set_format(&info->format, F_U8);
            else if (len == 2 && !strncasecmp("s8", t, 2)) file_type_set_format(&info->format, F_S8);
            else if (len == 3 && !strncasecmp("cu8", t, 3)) file_type_set_format(&info->format, F_CU8);
            else if (len == 3 && !strncasecmp("cs8", t, 3)) file_type_set_format(&info->format, F_CS8);
            else if (len == 3 && !strncasecmp("u16", t, 3)) file_type_set_format(&info->format, F_U16);
            else if (len == 3 && !strncasecmp("s16", t, 3)) file_type_set_format(&info->format, F_S16);
            else if (len == 3 && !strncasecmp("u32", t, 3)) file_type_set_format(&info->format, F_U32);
            else if (len == 3 && !strncasecmp("s32", t, 3)) file_type_set_format(&info->format, F_S32);
            else if (len == 3 && !strncasecmp("f32", t, 3)) file_type_set_format(&info->format, F_F32);
            else if (len == 3 && !strncasecmp("vcd", t, 3)) file_type_set_content(&info->format, F_VCD);
            else if (len == 3 && !strncasecmp("ook", t, 3)) file_type_set_content(&info->format, F_OOK);
            else if (len == 4 && !strncasecmp("cs16", t, 4)) file_type_set_format(&info->format, F_CS16);
            else if (len == 4 && !strncasecmp("cs32", t, 4)) file_type_set_format(&info->format, F_CS32);
            else if (len == 4 && !strncasecmp("cf32", t, 4)) file_type_set_format(&info->format, F_CF32);
            else if (len == 5 && !strncasecmp("logic", t, 5)) file_type_set_content(&info->format, F_LOGIC);
            else if (len == 3 && !strncasecmp("complex16u", t, 10)) file_type_set_format(&info->format, F_CU8);
            else if (len == 3 && !strncasecmp("complex16s", t, 10)) file_type_set_format(&info->format, F_CS8);
            else if (len == 4 && !strncasecmp("complex", t, 7)) file_type_set_format(&info->format, F_CF32);
            //else fprintf(stderr, "Skipping type (len %ld) %s\n", len, t);
        } else {
            p++; // skip non-alphanum char otherwise
        }
    }
}

// return the last colon not followed by a backslash, otherwise NULL
char const *last_plain_colon(char const *p)
{
    char const *found = NULL;
    char const *next = strchr(p, ':');
    while (next && next[1] != '\\') {
        found = next;
        next = strchr(next+1, ':');
    }
    return found;
}

/**
This will detect file info and overrides.

Parse "[0-9]+(\.[0-9]+)?[A-Za-z]"
 as frequency (suffix "M" or "[kMG]?Hz")
 or sample rate (suffix "k" or "[kMG]?sps")

Parse "[A-Za-z][0-9A-Za-z]+" as format or content specifier:

2ch formats: "cu8", "cs8", "cs16", "cs32", "cf32"
1ch formats: "u8", "s8", "s16", "u16", "s32", "u32", "f32"
text formats: "vcd", "ook"
content types: "iq", "i", "q", "am", "fm", "logic"

Parses left to right, with the exception of a prefix up to the last colon ":"
This prefix is the forced override, parsed last and removed from the filename.

All matches are case-insensitive.

default detection, e.g.: path/filename.am.s16
overrides, e.g.: am:s16:path/filename.ext
other styles are detected but discouraged, e.g.:
  am-s16:path/filename.ext, am.s16:path/filename.ext, path/filename.am_s16
*/
int parse_file_info(char const *filename, file_info_t *info)
{
    if (!filename || !*filename) {
        return 0;
    }

    info->spec = filename;

    char const *p = last_plain_colon(filename);
    if (p && p - filename < 64) {
        size_t len = p - filename;
        char forced[64];
        memcpy(forced, filename, len);
        forced[len] = '\0';
        p++;
        file_type(p, info);
        file_type(forced, info);
        info->path = p;
    } else {
        file_type(filename, info);
        info->path = filename;
    }
    info->raw_format = info->format;
    info->format = file_type_guess_auto_format(info->format);
    return info->format;
}

// Unit testing
#ifdef _TEST
void assert_file_type(int check, char const *spec)
{
    file_info_t info = {0};
    int ret = parse_file_info(spec, &info);
    if (check != ret) {
        fprintf(stderr, "\nTEST failed: determine_file_type(\"%s\", &foo) = %8x == %8x\n", spec, ret, check);
    } else {
        fprintf(stderr, ".");
    }
}

void assert_str_equal(char const *a, char const *b)
{
    if (a != b && strcmp(a, b)) {
        fprintf(stderr, "\nTEST failed: \"%s\" == \"%s\"\n", a, b);
    } else {
        fprintf(stderr, ".");
    }
}

int main(int argc, char **argv)
{
    fprintf(stderr, "Testing:\n");

    assert_str_equal(last_plain_colon("foo:bar:baz"), ":baz");
    assert_str_equal(last_plain_colon("foo"), NULL);
    assert_str_equal(last_plain_colon(":foo"), ":foo");
    assert_str_equal(last_plain_colon("foo:"), ":");
    assert_str_equal(last_plain_colon("foo:bar:C:\\path.txt"), ":C:\\path.txt");
    assert_str_equal(last_plain_colon("foo:bar:C:\\path.txt:baz"), ":C:\\path.txt:baz");

    assert_file_type(CU8_IQ, "cu8:");
    assert_file_type(CS16_IQ, "cs16:");
    assert_file_type(CF32_IQ, "cf32:");
    assert_file_type(S16_AM, "am:");
    assert_file_type(S16_AM, "am.s16:");
    assert_file_type(S16_AM, "am-s16:");
    assert_file_type(S16_AM, "am_s16:");
    assert_file_type(S16_AM, "s16.am:");
    assert_file_type(S16_AM, "s16-am:");
    assert_file_type(S16_AM, "s16_am:");
    assert_file_type(S16_AM, "am-s16.am:");
    assert_file_type(S16_FM, "fm:");
    assert_file_type(S16_FM, "fm.s16:");
    assert_file_type(S16_FM, "fm-s16:");
    assert_file_type(S16_FM, "fm_s16:");
    assert_file_type(S16_FM, "s16.fm:");
    assert_file_type(S16_FM, "s16-fm:");
    assert_file_type(S16_FM, "s16_fm:");
    assert_file_type(S16_FM, "fm+s16:");
    assert_file_type(S16_FM, "s16,fm:");

    assert_file_type(CU8_IQ, ".cu8");
    assert_file_type(CS16_IQ, ".cs16");
    assert_file_type(CF32_IQ, ".cf32");
    assert_file_type(S16_AM, ".am");
    assert_file_type(S16_AM, ".am.s16");
    assert_file_type(S16_AM, ".am-s16");
    assert_file_type(S16_AM, ".am_s16");
    assert_file_type(S16_AM, ".s16+am");
    assert_file_type(S16_AM, ".s16.am");
    assert_file_type(S16_AM, ".s16-am");
    assert_file_type(S16_AM, ".am-s16.am");
    assert_file_type(S16_FM, ".fm");
    assert_file_type(S16_FM, ".fm.s16");
    assert_file_type(S16_FM, ".fm-s16");
    assert_file_type(S16_FM, ".fm_s16");
    assert_file_type(S16_FM, ".fm+s16");
    assert_file_type(S16_FM, ".s16.fm");
    assert_file_type(S16_FM, ".s16-fm");
    assert_file_type(S16_FM, ".s16_fm");
    assert_file_type(S16_FM, ".s16,fm");

    fprintf(stderr, "\nDone!\n");
}
#endif /* _TEST */
