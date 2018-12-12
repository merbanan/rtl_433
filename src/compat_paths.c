// compat_paths addresses following compatibility issue:
// topic: default search paths for config file
// issue: Linux and Windows use different common paths for config files
// solution: provide specific default paths for each system

#ifndef _WIN32
// Linux variant

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h> 
#include <stdlib.h>

char **compat_getDefaultConfPaths()
{
    static char *pointers[5] = { NULL };
    static char buf[256] = "";
    if (!pointers[0]) {
        pointers[0] = "rtl_433.conf";
        snprintf(buf, sizeof(buf), "%s%s", getenv("HOME"), "/.rtl_433.conf");
        pointers[1] = buf;
        pointers[2] = "/usr/local/etc/rtl_433.conf";
        pointers[3] = "/etc/rtl_433.conf";
        pointers[4] = NULL;
    };
    return pointers;
}

#else
// Windows variant

#include <stdbool.h>
#include <stddef.h>
#include <Shlobj.h>

char **compat_getDefaultConfPaths()
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
#endif // _WIN32 / !_WIN32
