#ifndef INCLUDE_COMPAT_PTHREAD_H_
#define INCLUDE_COMPAT_PTHREAD_H_

#ifndef THREADS

// explicit request for "no threads"

// no pthreads only on MSC, use compat on all WIN32 anyway
//#elif _MSC_VER>=1200
#elif _WIN32

#include <windows.h>
#include <process.h>
#define THREAD_CALL                     __stdcall
#define THREAD_RETURN                   unsigned int
typedef HANDLE                          pthread_t;
#define pthread_create(tp, x, p, d)     ((*tp=(HANDLE)_beginthreadex(NULL, 0, p, d, 0, NULL)) == NULL ? -1 : 0)
#define pthread_cancel(th)              (!TerminateThread(th, 0))
#define pthread_join(th, p)             (WaitForSingleObject(th, INFINITE))
#define pthread_equal(a, b)             ((a) == (b))
#define pthread_self()                  (GetCurrentThread())

typedef HANDLE                          pthread_mutex_t;
#define pthread_mutex_init(mp, a)       ((*mp = CreateMutex(NULL, FALSE, NULL)) == NULL ? -1 : 0)
#define pthread_mutex_destroy(mp)       (CloseHandle(*mp) == 0 ? -1 : 0)
#define pthread_mutex_lock(mp)          (WaitForSingleObject(*mp, INFINITE) == WAIT_OBJECT_0 ? 0 : -1)
#define pthread_mutex_unlock(mp)        (ReleaseMutex(*mp) == 0 ? -1 : 0)

typedef CONDITION_VARIABLE              pthread_cond_t;
#define pthread_cond_init(cp, a)        (InitializeConditionVariable(cp))
#define pthread_cond_destroy(cp)        (0)
#define pthread_cond_wait(cp, mp)       (SleepConditionVariableCS(cp, *mp, INFINITE) ? 0 : 1)
#define pthread_cond_signal(cp)         (WakeConditionVariable(cp))
#define pthread_cond_broadcast(cp)      (WakeAllConditionVariable(cp))

// #elif __GNUC__>3 || (__GNUC__==3 && __GNUC_MINOR__>3)
#else

#include <pthread.h>
#define THREAD_CALL
#define THREAD_RETURN                   void*

#endif

#endif /* INCLUDE_COMPAT_PTHREAD_H_ */
