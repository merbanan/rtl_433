/** @file
    SigMF basic file read and write support.

    Copyright (C) 2023 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/*
NOTES:
V7, USTAR, PAX, GNU, OLDGNU, PAX
ignore pax files of object types: 'x' and 'g'

https://mort.coffee/home/tar/
https://pubs.opengroup.org/onlinepubs/9699919799/utilities/pax.html

*/

/*
00000000  62 35 31 32 2e 62 69 6e  00 00 00 00 00 00 00 00  |b512.bin........|
00000010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000060  00 00 00 00 30 30 30 36  34 34 20 00 30 30 30 37  |....000644 .0007|
00000070  36 35 20 00 30 30 30 30  32 34 20 00 30 30 30 30  |65 .000024 .0000|
00000080  30 30 30 31 30 30 30 20  31 34 35 30 31 35 36 30  |0001000 14501560|
00000090  35 32 33 20 30 31 32 33  33 35 00 20 30 00 00 00  |523 012335. 0...|
000000a0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000100  00 75 73 74 61 72 00 30  30 7a 61 6e 79 00 00 00  |.ustar.00zany...|
00000110  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000120  00 00 00 00 00 00 00 00  00 73 74 61 66 66 00 00  |.........staff..|
00000130  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00000140  00 00 00 00 00 00 00 00  00 30 30 30 30 30 30 20  |.........000000 |
00000150  00 30 30 30 30 30 30 20  00 00 00 00 00 00 00 00  |.000000 ........|
00000160  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00000800
*/

#include "sigmf.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
    #include <io.h>
    #include <fcntl.h>
    #ifdef _MSC_VER
        #define F_OK 0
    #endif
#endif
#ifndef _MSC_VER
    #include <unistd.h>
#endif

#ifdef _MSC_VER
    #ifndef strncasecmp // Microsoft Visual Studio
        #define strncasecmp _strnicmp
    #endif
#else
    #include <strings.h>
#endif

#include "logger.h"
#include "fatal.h"
#include "microtar.h"
#include "jsmn.h"

// Helper

static int path_has_extension(char const *path, char const *ext)
{
    if (!path || !ext) {
        return 0;
    }
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    return path_len >= ext_len && !strncasecmp(ext, path + path_len - ext_len, ext_len);
}

static int jsoneq(char const *json, jsmntok_t const *tok, char const *s)
{
    if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
            strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
        return 0;
    }
    return -1;
}

static char *jsondup(char const *json, jsmntok_t const *tok)
{
    int len = tok->end - tok->start;
    char *p = malloc(len + 1);
    if (!p) {
        WARN_MALLOC("jsondup()");
        return NULL;
    }
    p[len] = '\0';
    return memcpy(p, json + tok->start, len);
}

/*
{
    "global": {
        "core:datatype": "cu8",
        "core:sample_rate": 250000,
        "core:recorder": "rtl_433"
        "core:description": "bresser_3ch/gfile001.cu8"
        "core:version": "1.0.0"
    },
    "captures": [
        {
            "core:sample_start": 0,
            "core:frequency": 433920000
        }
    ],
    "annotations": []
}
*/
static int json_parse(sigmf_t *sigmf, char const *json)
{
    int i = 0;
    int r;
    jsmn_parser p;
    jsmntok_t t[128]; // We expect around 20 tokens

    jsmn_init(&p);
    r = jsmn_parse(&p, json, strlen(json), t, sizeof(t) / sizeof(t[0]));
    if (r < 0) {
        print_logf(LOG_WARNING, __func__, "Failed to parse JSON: %d", r);
        return -1;
    }

    // Expect the top-level element to be an object
    if (r < 1 || t[0].type != JSMN_OBJECT) {
        print_log(LOG_WARNING, __func__, "Object expected");
        return -1;
    }

    //for (int j = 0; j < r; j++) {
    //    print_logf(LOG_WARNING, __func__, "token (%d): %.*s", t[j].size, t[j].end - t[j].start, json + t[j].start);
    //}

    // Loop over all keys of the root object
    for (int obj0_items = t[i].size; obj0_items > 0; obj0_items--) {
        i++;
        //print_logf(LOG_WARNING, __func__, "0 PARSING: %.*s", t[i].end - t[i].start, json + t[i].start);
        if (jsoneq(json, &t[i], "global") == 0) {
            i++;
            // Expect the global element to be an object
            if (i >= r || t[i].type != JSMN_OBJECT) {
                print_log(LOG_WARNING, __func__, "Object expected");
                return -1;
            }
            // Loop over all keys of the global object
            for (int obj1_items = t[i].size; obj1_items > 0; obj1_items--) {
                i++;
                //print_logf(LOG_WARNING, __func__, "1 PARSING: %.*s", t[i].end - t[i].start, json + t[i].start);
                if (jsoneq(json, &t[i], "core:datatype") == 0) {
                    i++;
                    free(sigmf->datatype);
                    sigmf->datatype = jsondup(json, &t[i]);
                    printf("SigMF datatype: %s\n", sigmf->datatype);
                }
                else if (jsoneq(json, &t[i], "core:sample_rate") == 0) {
                    i++;
                    char *endptr = NULL;
                    double val = strtod(json + t[i].start, &endptr);
                    // compare endptr to t[i].end
                    sigmf->sample_rate = (uint32_t)val;
                    printf("SigMF sample_rate: %u\n", sigmf->sample_rate);
                }
                else if (jsoneq(json, &t[i], "core:recorder") == 0) {
                    i++;
                    free(sigmf->recorder);
                    sigmf->recorder = jsondup(json, &t[i]);
                    printf("SigMF recorder: %s\n", sigmf->recorder);
                }
                else if (jsoneq(json, &t[i], "core:description") == 0) {
                    i++;
                    free(sigmf->description);
                    sigmf->description = jsondup(json, &t[i]);
                    printf("SigMF description: %s\n", sigmf->description);
                }
                else if (jsoneq(json, &t[i], "core:version") == 0) {
                    i++;
                    if (jsoneq(json, &t[i], "1.0.0") != 0) {
                        print_logf(LOG_WARNING, __func__, "Expected version 1.0.0 but found: %.*s", t[i].end - t[i].start, json + t[i].start);
                    }
                }
                else {
                    print_logf(LOG_WARNING, __func__, "Unexpected key: %.*s", t[i].end - t[i].start, json + t[i].start);
                }
            }
        }
        else if (jsoneq(json, &t[i], "captures") == 0) {
            i++;
            // Expect the captures element to be an array
            if (i >= r || t[i].type != JSMN_ARRAY) {
                print_log(LOG_WARNING, __func__, "Array expected");
                return -1;
            }
            for (int obj1_items = t[i].size; obj1_items > 0; obj1_items--) {
                i++;

                // Expect the capture elements to be an object
                if (i >= r || t[i].type != JSMN_OBJECT) {
                    print_log(LOG_WARNING, __func__, "Object expected");
                    return -1;
                }
                // Loop over all keys of a capture object
                for (int obj2_items = t[i].size; obj2_items > 0; obj2_items--) {
                    i++;
                    if (jsoneq(json, &t[i], "core:sample_start") == 0) {
                        i++;
                        char *endptr = NULL;
                        uint32_t val   = strtoul(json + t[i].start, &endptr, 10);
                        // compare endptr to t[i].end
                        sigmf->first_sample_start = val;
                        printf("SigMF first_sample_start: %u\n", sigmf->first_sample_start);
                    }
                    else if (jsoneq(json, &t[i], "core:global_index") == 0) {
                        i++; // skip value
                    }
                    else if (jsoneq(json, &t[i], "core:frequency") == 0) {
                        i++;
                        char *endptr = NULL;
                        double val   = strtod(json + t[i].start, &endptr);
                        // compare endptr to t[i].end
                        sigmf->first_frequency = (uint32_t)val;
                        printf("SigMF first_frequency: %u\n", sigmf->first_frequency);
                    }
                    else if (jsoneq(json, &t[i], "core:datetime") == 0) {
                        i++; // skip value
                    }
                    else if (jsoneq(json, &t[i], "core:header_bytes") == 0) {
                        i++; // skip value
                    }
                    else {
                        print_logf(LOG_WARNING, __func__, "Unexpected key: %.*s", t[i].end - t[i].start, json + t[i].start);
                    }
                }
            }
        }
        else if (jsoneq(json, &t[i], "annotations") == 0) {
            i++;
            // Expect the annotations element to be an array
            if (i >= r || t[i].type != JSMN_ARRAY) {
                print_log(LOG_WARNING, __func__, "Array expected");
                return -1;
            }
            for (int obj1_items = t[i].size; obj1_items > 0; obj1_items--) {
                i++;

                // Expect the annotation elements to be an object
                if (i >= r || t[i].type != JSMN_OBJECT) {
                    print_log(LOG_WARNING, __func__, "Object expected");
                    return -1;
                }
                // Loop over all keys of a annotation object
                for (int obj2_items = t[i].size; obj2_items > 0; obj2_items--) {
                    i++;
                    // core:sample_start
                    // core:sample_count
                    // core:freq_lower_edge
                    // core:freq_upper_edge
                    // core:label
                    // core:generator
                    // core:comment
                    // core:uuid
                    i++; // TODO: implement value reader...
                }
            }
        }
        else {
            print_logf(LOG_WARNING, __func__, "Unexpected key: %.*s", t[i].end - t[i].start, json + t[i].start);
        }
    }

    return 0;
}

static int sigmf_write_meta(sigmf_t *sigmf, mtar_t *tar)
{
    (void)sigmf;

    // NOTE: we need e.g. "core:dataset": "samples.cu12" for uncommon formats
    char json[1024] = {0};
    snprintf(json, sizeof(json), "{\
    \"global\" : {\
        \"core:datatype\" : \"%s\",\
        \"core:sample_rate\" : %u,\
        \"core:recorder\" : \"%s\",\
        \"core:version\" : \"1.0.0\"\
    },\
    \"captures\" : [\
        {\
            \"core:sample_start\" : %u,\
            \"core:frequency\" : %u\
        }\
    ],\
    \"annotations\" : []\
}\
",
            sigmf->datatype,
            sigmf->sample_rate,
            sigmf->recorder,
            sigmf->first_sample_start,
            sigmf->first_frequency);

    size_t json_len = strlen(json);
    mtar_write_file_header(tar, "foobar.sigmf-meta", json_len);
    mtar_write_data(tar, json, json_len);

    return 0;
}

// API

int sigmf_valid_filename(char const *p)
{
    if (!p) {
        return 0;
    }
    int len = strlen(p);
    return len >= 6 && !strncasecmp(".sigmf", p + len - 6, 6);
}

int sigmf_reader_open(sigmf_t *sigmf, char const *path)
{
    int is_sigmf = sigmf_valid_filename(path);
    if (!is_sigmf){
        print_logf(LOG_WARNING, "Input", "SigMF input file must have .sigmf extension");
        // return -1;
    }

    mtar_header_t h;
    char *stream_name = NULL;

    // Open archive for reading
    mtar_open(&sigmf->mtar, path, "r");

    // Check all file names
    while ((mtar_read_header(&sigmf->mtar, &h)) != MTAR_ENULLRECORD) {
        print_logf(LOG_DEBUG, "Input", "SigMF input cotains: %s (%u bytes)", h.name, h.size);

        // Skip all non-regular files
        if (h.type != MTAR_TREG) {
            print_logf(LOG_WARNING, "Input", "SigMF input file contains a non-regular file");
            mtar_next(&sigmf->mtar);
            continue;
        }

        // Warn if collection is present
        if (path_has_extension(h.name, ".sigmf-collection")) {
            print_logf(LOG_WARNING, "Input", "SigMF input file contains a collection which is no supported");
        }

        // Grab the first stream
        if (path_has_extension(h.name, ".sigmf-meta")) {
            if (stream_name && strcmp(h.name, stream_name)) {
                print_logf(LOG_NOTICE, "Input", "SigMF input file contains updated meta file");
                free(stream_name);
                stream_name = NULL;
                // free(meta_json);
            }
            if (stream_name) {
                print_logf(LOG_WARNING, "Input", "SigMF input file contains multiple streams");
            }
            else {
                stream_name = strdup(h.name);
                if (!stream_name) {
                    WARN_STRDUP("add_dumper()");
                }
                // read meta
                char *p = calloc(1, h.size + 1);
                if (!p) {
                    WARN_MALLOC("sigmf_reader_open()");
                    free(stream_name);
                    return -1;
                }
                mtar_read_data(&sigmf->mtar, p, h.size);
                // printf("%s", p);
                // decode JSON and copy meta
                json_parse(sigmf, p);
                free(p);
            }

            // Continue to read through all tar entries to check for errors
        }
        free(stream_name);

        mtar_next(&sigmf->mtar);
    }

    if (!stream_name) {
        print_logf(LOG_ERROR, "Input", "SigMF input file with no streams");
        return -1;
    }

    // Mangle stream name
    size_t name_len = strlen(stream_name);
    strncpy(stream_name + name_len - 4, "data", 4);

    // Load and print contents of file "foo.sigmf-data"
    // NOTE: finds the first instance but should theoretically use the last one
    int r = mtar_find(&sigmf->mtar, stream_name, &h);
    if (r) {
        print_logf(LOG_ERROR, "Input", "SigMF input file with no stream data");
        return -1;
    }

    // Seek past header
    size_t sizeof_mtar_raw_header_t = 512; // sizeof(mtar_raw_header_t)
    int err = mtar_seek(&sigmf->mtar, sigmf->mtar.pos + sizeof_mtar_raw_header_t);
    if (err) {
        return err;
    }

    return 0;
}

int sigmf_reader_close(sigmf_t *sigmf)
{
    return fclose(sigmf->mtar.stream);
}

int sigmf_writer_open(sigmf_t *sigmf, char const *path, int overwrite)
{
    if (access(path, F_OK) == 0) {
        if (!overwrite) {
            print_logf(LOG_FATAL, "Output", "SigMF output file %s already exists, exiting", path);
            exit(1);
        }
        else {
            print_logf(LOG_NOTICE, "Output", "SigMF output file %s already exists, overwriting", path);
        }
    }

    // Open archive for writing
    mtar_open(&sigmf->mtar, path, "w");

    sigmf_write_meta(sigmf, &sigmf->mtar);

    mtar_write_file_header(&sigmf->mtar, "foobar.sigmf-data", sigmf->data_len);
    //mtar_write_data(&sigmf->mtar, data, size);

    sigmf->data_offset = ftell(sigmf->mtar.stream);

    return 0;
    }

int sigmf_writer_close(sigmf_t *sigmf)
{
    char const padding[512] = {0};
    long f_pos = ftell(sigmf->mtar.stream);
    long d_len = f_pos - sigmf->data_offset;
    /* Write padding if needed */
    if (f_pos % 512) {
        fwrite(padding, 1, 512 - f_pos % 512, sigmf->mtar.stream);
    }
    /* Write two NULL records */
    fwrite(padding, 1, 512, sigmf->mtar.stream);
    fwrite(padding, 1, 512, sigmf->mtar.stream);

    // Set the file length header if needed
    if (sigmf->data_len != d_len) {
        // Write header again
        int r = fseek(sigmf->mtar.stream, sigmf->data_offset - 512, SEEK_SET);
        if (r) {
            print_logf(LOG_ERROR, "Output", "SigMF output file seek failed");
        } else {
            r = mtar_write_file_header(&sigmf->mtar, "foobar.sigmf-data", d_len);
            if (r) {
                print_logf(LOG_ERROR, "Output", "SigMF output file header rewrite failed");
            }
        }
    }
    // return mtar_close(&sigmf->mtar);
    return fclose(sigmf->mtar.stream);
}

int sigmf_free_items(sigmf_t *sigmf)
{
    // TODO: check if base tar file is free
    //sigmf->mtar...
    free(sigmf->datatype);
    sigmf->datatype = NULL;
    free(sigmf->recorder);
    sigmf->recorder = NULL;
    free(sigmf->description);
    sigmf->description = NULL;
    return 0;
}
