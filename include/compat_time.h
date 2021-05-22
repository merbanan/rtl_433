/** @file
    compat_time addresses compatibility time and alarm() functions.

    topic: high-resolution timestamps
    issue: <sys/time.h> is not available on Windows systems
    solution: provide a compatible version for Windows systems
*/

#ifndef INCLUDE_COMPAT_TIME_H_
#define INCLUDE_COMPAT_TIME_H_

// ensure struct timeval is known
#ifdef _WIN32
#include <winsock2.h>
#include <signal.h>
#else
#include <sys/time.h>
#endif

/** Subtract `struct timeval` values.

    @param[out] result time difference result
    @param x first time value
    @param y second time value
    @return 1 if the difference is negative, otherwise 0.
*/
int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y);

// platform-specific functions

#ifdef _WIN32
int gettimeofday(struct timeval *tv, void *tz);
int win_alarm(unsigned seconds);

#define alarm(sec)  win_alarm(sec)
#define SIGALRM     SIGBREAK  /* No SIGUSRx on Windows. Use this */
#endif

#endif  /* INCLUDE_COMPAT_TIME_H_ */
