// platform-specific stuff
// only used for single functions needing different treatment

#ifdef _WIN32

#include <stdbool.h>
#include <stddef.h>

#include <Shlobj.h>
#include "platform.h"

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000Ui64
#else
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000ULL
#endif

char **pf_getDefaultConfPaths()
{
    static char bufs[3][256];
    static char *pointers[4] = { NULL };
    if (pointers[0]) return pointers;
    // Working directory, i.e. where the binary is located
    if (GetModuleFileName(NULL, bufs[0], sizeof(bufs[0]))) {
        char *last_slash = max(strrchr(bufs[0], '\\'), strrchr(bufs[0], '/'));
        if (last_slash) *last_slash = 0;
        strcat_s(bufs[0], sizeof(bufs[0]), "\\rtl_433.conf");
        pointers[0] = bufs[0];
    }
    else {
        pointers[0] = NULL;
    }
    // Local per user configuration files (e.g. Win7: C:\Users\myusername\AppData\Local\rtl_433\rtl_433.conf)
    if (SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, bufs[1]) == S_OK) {
        strcat_s(bufs[1], sizeof(bufs[1]), "\\rtl_433\\rtl_433.conf");
        pointers[1] = bufs[1];
    }
    else {
        pointers[1] = NULL;
    }
    // Per machine configuration data (e.g. Win7: C:\ProgramData\rtl_433\rtl_433.conf)
    if (SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, 0, bufs[2]) == S_OK) {
        strcat_s(bufs[2], sizeof(bufs[2]), "\\rtl_433\\rtl_433.conf");
        pointers[2] = bufs[2];
    }
    else {
        pointers[2] = NULL;
    }
    pointers[3] = NULL;
    return pointers;
}

void pf_get_time_now(pf_timeval *tv)
{
    FILETIME ft;
    unsigned __int64 t64;
    GetSystemTimeAsFileTime(&ft);
    t64 = (((unsigned __int64) ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    t64 /= 10; // convert to microseconds
    t64 -= DELTA_EPOCH_IN_MICROSECS; // convert file time to unix epoch
    tv->tv_sec = (long)(t64 / 1000000UL);
    tv->tv_usec = (long)(t64 % 1000000UL);
}

#endif // _WIN32
