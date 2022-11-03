/**
 * Emulation of the Posix function `alarm()`
 * using `CreateTimerQueueTimer()`.
 *
 * \ref https://docs.microsoft.com/en-us/windows/win32/api/threadpoollegacyapiset/nf-threadpoollegacyapiset-createtimerqueuetimer
 */
#include <stdio.h>
#include "compat_alarm.h"

#ifdef HAVE_win_alarm /* rest of file */

static HANDLE alarm_hnd = INVALID_HANDLE_VALUE;
static int    alarm_countdown;

/**
 * The timer-callback that performs the countdown.
 */
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

/**
 * Destroy the timer.<br>
 * Called as an `atexit()` function.
 */
static void alarm_delete(void)
{
    if (!alarm_hnd || alarm_hnd == INVALID_HANDLE_VALUE)
       return;
    signal(SIGALRM, SIG_IGN);
    DeleteTimerQueueTimer(NULL, alarm_hnd, NULL);
    alarm_hnd = INVALID_HANDLE_VALUE;
}

/**
 * Create a kernel32 timer once.
 */
static void alarm_create(void)
{
    if (alarm_hnd && alarm_hnd != INVALID_HANDLE_VALUE)
       return;

    if (!CreateTimerQueueTimer(&alarm_hnd, NULL, alarm_handler,
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

/**
 * Emulate an `alarm(sec)` function.
 *
 *  @param[in] seconds  the number of seconds to countdown before a `raise(SIGALRM)` is done.<br>
 *                      if `seconds == 0` the `alarm_handler()` will do nothing.
 */
int win_alarm(unsigned seconds)
{
  alarm_countdown = seconds;
  alarm_create();
  return (0);
}
#else

/*
 * Just so this compilation unit isn't empty.
 */
int win_alarm(unsigned seconds);
int win_alarm(unsigned seconds)
{
   (void) seconds;
   return (0);
}
#endif /* HAVE_win_alarm */
