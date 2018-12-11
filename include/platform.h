// platform-specific stuff
// only used for single functions needing different treatment

#ifndef PLATFORM_INCLUDED
#define PLATFORM_INCLUDED

// 1) ensure struct timeval is known
// we have to typedef it as pf_timeval since windows systems have a conflicting definition in winsock(2).h
#ifdef _WIN32
typedef struct _pf_timeval {
    long    tv_sec;         /* seconds */
    long    tv_usec;        /* and microseconds */
} pf_timeval;

#else
#include <sys/time.h>
typedef struct timeval pf_timeval;
#endif

// 2) platform-specific functions
char **pf_getDefaultConfPaths();       // get default search paths for rtl_433 config file
void pf_get_time_now(pf_timeval *tv);  // get high precision time

#endif  // PLATFORM_INCLUDED
