// compat_time addresses following compatibility issue:
// topic: high-resolution timestamps
// issue: <sys/time.h> is not available on Windows systems
// solution: provide a compatible version for Windows systems

#ifndef _WIN32
// Linux variant

// just so the compilation unit isn't empty
int _compat_time(void)
{
    return 0;
}

#else
// Windows variant

#include <stdbool.h>
#include <stddef.h>

#include "compat_time.h"

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000Ui64
#else
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000ULL
#endif

int gettimeofday(struct timeval *tv, void *tz)
{
    if (tz)
        return -1; // we don't support TZ

    FILETIME ft;
    unsigned __int64 t64;
    GetSystemTimeAsFileTime(&ft);
    t64 = (((unsigned __int64)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    t64 /= 10; // convert to microseconds
    t64 -= DELTA_EPOCH_IN_MICROSECS; // convert file time to unix epoch
    tv->tv_sec = (long)(t64 / 1000000UL);
    tv->tv_usec = (long)(t64 % 1000000UL);

    return 0;
}

#endif // _WIN32 / !_WIN32
