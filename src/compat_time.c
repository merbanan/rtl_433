// compat_time addresses following compatibility issue:
// topic: high-resolution timestamps
// issue: <sys/time.h> is not available on Windows systems
// solution: provide a compatible version for Windows systems

#include "compat_time.h"

#ifdef _WIN32

#include <stdbool.h>
#include <stddef.h>

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

#endif // _WIN32

int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
    // Perform the carry for the later subtraction by updating y
    if (x->tv_usec < y->tv_usec) {
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
        y->tv_usec -= 1000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_usec - y->tv_usec > 1000000) {
        int nsec = (x->tv_usec - y->tv_usec) / 1000000;
        y->tv_usec += 1000000 * nsec;
        y->tv_sec -= nsec;
    }

    // Compute the time difference, tv_usec is certainly positive
    result->tv_sec  = x->tv_sec - y->tv_sec;
    result->tv_usec = x->tv_usec - y->tv_usec;

    // Return 1 if result is negative
    return x->tv_sec < y->tv_sec;
}
