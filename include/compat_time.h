/** @file
    compat_time addresses compatibility time functions.

    topic: high-resolution timestamps
    issue: <sys/time.h> is not available on Windows systems
    solution: provide a compatible version for Windows systems
*/

#ifndef INCLUDE_COMPAT_TIME_H_
#define INCLUDE_COMPAT_TIME_H_

// ensure struct timeval is known
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

// platform-specific functions

#ifdef _WIN32
int gettimeofday(struct timeval *tv, void *tz);
#endif

#endif  /* INCLUDE_COMPAT_TIME_H_ */
