/** @file
    @brief compat_alarm adds an alarm() function for Windows.

    Except for MinGW-w64 when `_POSIX` and/or `__USE_MINGW_ALARM`
    is defined
 */

#ifndef INCLUDE_COMPAT_ALARM_H_
#define INCLUDE_COMPAT_ALARM_H_

#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#include <io.h>    /* alarm() for MinGW is possibly here */

#if !defined(_POSIX) && !defined(__USE_MINGW_ALARM)
int win_alarm(unsigned seconds);
#define alarm(sec)  win_alarm(sec)
#define HAVE_win_alarm
#endif

/* No SIGUSRx on Windows. Use this unless MinGW-w64
 * has support for it (untested by me).
 */
#if !defined(__USE_MINGW_ALARM)
#define SIGALRM  SIGBREAK
#endif

#endif  /* _WIN32 */
#endif  /* INCLUDE_COMPAT_ALARM_H_ */
