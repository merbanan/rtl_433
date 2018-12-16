// compat_time addresses following compatibility issue:
// topic: high-resolution timestamps
// issue: <sys/time.h> is not available on Windows systems
// solution: provide a compatible version for Windows systems

#ifndef COMPAT_TIME_H
#define COMPAT_TIME_H

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

#endif  // COMPAT_TIME_H
