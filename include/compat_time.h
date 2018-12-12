// compat_time addresses following compatibility issue:
// topic: high-resolution timestamps
// issue: <sys/time.h> is not available on Windows systems
// solution: provide a compatible version for Windows systems

#ifndef COMPAT_TIME_INCLUDED
#define COMPAT_TIME_INCLUDED

// ensure struct timeval is known
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

// platform-specific functions
void compat_get_time_now(struct timeval *tv);  // get high precision time

#endif  // COMPAT_TIME_INCLUDED
