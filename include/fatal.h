/** @file
    Fatal abort and warning macros for allocs.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_FATAL_H_
#define INCLUDE_FATAL_H_

#define STRINGIFYX(x) #x
#define STRINGIFY(x) STRINGIFYX(x)
#define FILE_LINE __FILE__ ":" STRINGIFY(__LINE__)
#define FATAL(what) do { fprintf(stderr, "FATAL: " what " from " FILE_LINE "\n"); exit(1); } while (0)
#define FATAL_MALLOC(what) FATAL("low memory? malloc() failed in " what)
#define FATAL_CALLOC(what) FATAL("low memory? calloc() failed in " what)
#define FATAL_REALLOC(what) FATAL("low memory? realloc() failed in " what)
#define FATAL_STRDUP(what) FATAL("low memory? strdup() failed in " what)
#define WARN(what) fprintf(stderr, "WARNING: " what " from " FILE_LINE "\n")
#define WARN_MALLOC(what) WARN("low memory? malloc() failed in " what)
#define WARN_CALLOC(what) WARN("low memory? calloc() failed in " what)
#define WARN_REALLOC(what) WARN("low memory? realloc() failed in " what)
#define WARN_STRDUP(what) WARN("low memory? strdup() failed in " what)

/*
    Use like this:

    char *buf = malloc(size);
    if (!buf)
        FATAL_MALLOC("my_func()");

*/

#endif /* INCLUDE_FATAL_H_ */
