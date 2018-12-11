// platform-specific stuff
// only used for single functions needing different treatment

#ifndef _WIN32

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h> 

#include "platform.h"

char **pf_getDefaultConfPaths()
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

void pf_get_time_now(pf_timeval *tv)
{
    int ret = gettimeofday(tv, NULL);
    if (ret)
        perror("gettimeofday");
}

#endif // !_WIN32
