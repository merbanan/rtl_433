// compat_time addresses following compatibility issue:
// topic: high-resolution timestamps
// issue: <sys/time.h> is not available on Windows systems
// solution: provide a compatible version for Windows systems

#include "compat_time.h"

#ifdef _WIN32

#include <stdio.h>
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

/* Emulation of alarm() using CreateTimerQueueTimer().
 * Ref:
 *   https://docs.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-createtimerqueetimer
 */
static HANDLE alarm_hnd = INVALID_HANDLE_VALUE;
static int    alarm_countdown;

void CALLBACK alarm_handler(PVOID param, BOOLEAN timer_fired)
{
    if (alarm_countdown > 0)  {
       alarm_countdown--;
       if (alarm_countdown == 0)
          raise(SIGALRM);
    }
    (void) timer_fired;
    (void) param;
}

static void alarm_delete(void)
{
    if (!alarm_hnd || alarm_hnd == INVALID_HANDLE_VALUE)
       return;
    signal(SIGALRM, SIG_IGN);
    DeleteTimerQueueTimer(NULL, alarm_hnd, NULL);
    CloseHandle(alarm_hnd);
    alarm_hnd = INVALID_HANDLE_VALUE;
}

static void alarm_create(void)
{
    if (alarm_hnd && alarm_hnd != INVALID_HANDLE_VALUE)
       return;

    if (!CreateTimerQueueTimer (&alarm_hnd, NULL, alarm_handler,
                                NULL,
                                1000,  /* call alarm_handler() after 1 sec */
                                1000,  /* an do it periodically every seconds */
                                WT_EXECUTEDEFAULT | WT_EXECUTEINTIMERTHREAD)) {
        fprintf(stderr, "CreateTimerQueueTimer() failed %lu\n", GetLastError());
        alarm_hnd = NULL;
    }
    else
        atexit(alarm_delete);
}

int win_alarm(unsigned seconds)
{
  alarm_countdown = seconds;
  alarm_create();
  return (0);
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
