/** @file
    Light-weight (i.e. dumb) config-file parser.

    - a valid config line is a keyword followed by an argument to the end of line
    - whitespace around the keyword is ignored
    - comments start with a hash sign, no inline comments, empty lines are ok.
    - whitespace is space and tab

    Copyright (C) 2018 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "confparse.h"

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#ifdef _MSC_VER
#define F_OK 0
#define R_OK (1<<2)
#endif
#endif
#ifndef _MSC_VER
#include <unistd.h>
#endif

static off_t fsize(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return st.st_size;

    return -1;
}

int hasconf(char const *path)
{
    return !access(path, R_OK);
}

char *readconf(char const *path)
{
    FILE *fp;
    char *conf;
    off_t file_size = fsize(path);

    fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open \"%s\"\n", path);
        return NULL;
    }

    conf = (char *)malloc(file_size + 1);
    if (conf == NULL) {
        fprintf(stderr, "Failed to allocate memory for \"%s\"\n", path);
        fclose(fp);
        return NULL;
    }

    off_t n_read = fread(conf, sizeof(char), file_size, fp);
    fclose(fp);
    if (n_read != file_size) {
        fprintf(stderr, "Failed to read \"%s\"\n", path);
        free(conf);
        return NULL;
    }
    conf[file_size] = '\0';

    return conf;
}

int getconf(char **conf, struct conf_keywords const keywords[], char **arg)
{
    // abort if no conf or EOF
    if (!conf || !*conf || !**conf)
        return -1;

    char *p = *conf;

    // skip whitespace and comments
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '#')
        if (*p++ == '#') {
            while (*p && *p != '\r' && *p != '\n') p++;
        }

    // abort if EOF
    if (!*p)
        return -1;

    // parse keyword
    char *kw = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
        p++;
    if (*p)
        *p++ = '\0';

    // parse arg
    while (*p == ' ' || *p == '\t')
        p++;
    char *ka = p;
    if (*p == '{') { // quoted
        ka = ++p;
        while (*p) { // skip to end-quote
            while (*p && *p != '}')
                p++;
            char *e = p; // possible end-quote
            if (*p)
                p++;
            // skip ws
            while (*p == ' ' || *p == '\t')
                p++;
            // check if proper end-quote
            if (!*p || *p == '\r' || *p == '\n' || *p == '#') {
                *e = '\0';
                break;
            }
        }

    } else { // not quoted
        while (*p && *p != '\r' && *p != '\n')
            p++;
        if (*p)
            *p++ = '\0';
    }

    // set OUT vars
    if (arg)
        *arg = ka;
    *conf = p;

    // decode keyword
    for (; keywords->keyword; keywords++) {
        if (!strcmp(keywords->keyword, kw)) {
            return keywords->key;
        }
    }

    fprintf(stderr, "Unknown keyword \"%s\"\n", kw);
    return '?';
}
