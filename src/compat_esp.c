/** @file
    ESP-IDF (newlib and lwIP) compatibility.

    topic: POSIX and netdb functions
    issue: newlib and lwIP lack a few that rtl_433 calls; it does not link without them
    solution: provide minimal versions on ESP-IDF. There are no signals, no processes and usually no
    filesystem; the network outputs only need a name for themselves and numeric addresses.

    Copyright (C) 2026 David Coulson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "compat_esp.h"

#include <stdio.h>

#ifdef ESP_PLATFORM

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

char const *gai_strerror(int ecode)
{
    static char buf[32];
    snprintf(buf, sizeof(buf), "getaddrinfo error %d", ecode);
    return buf;
}

// rtl_433 sets up SIGINT/SIGTERM handlers; there are no signals here
int sigaction(int sig, struct sigaction const *act, struct sigaction *old)
{
    (void)sig;
    (void)act;
    if (old)
        memset(old, 0, sizeof(*old));
    return 0;
}

// Used to probe for config files
int access(char const *path, int mode)
{
    (void)mode;
    struct stat st;
    return stat(path, &st);
}

// Names this receiver in MQTT, syslog and InfluxDB output
int gethostname(char *name, size_t len)
{
    if (len == 0)
        return -1;
    strncpy(name, "rtl433-esp", len);
    name[len - 1] = '\0';
    return 0;
}

// Numeric addresses only, which is all rtl_433 asks for (NI_NUMERICHOST | NI_NUMERICSERV)
int getnameinfo(struct sockaddr const *addr, socklen_t addrlen, char *host, socklen_t hostlen,
        char *serv, socklen_t servlen, int flags)
{
    (void)addrlen;
    (void)flags;
    void const *src;
    unsigned port;
    if (addr->sa_family == AF_INET) {
        src  = &((struct sockaddr_in const *)addr)->sin_addr;
        port = ntohs(((struct sockaddr_in const *)addr)->sin_port);
    }
#if LWIP_IPV6
    else if (addr->sa_family == AF_INET6) {
        src  = &((struct sockaddr_in6 const *)addr)->sin6_addr;
        port = ntohs(((struct sockaddr_in6 const *)addr)->sin6_port);
    }
#endif
    else {
        return -1;
    }
    if (host && hostlen > 0 && inet_ntop(addr->sa_family, src, host, hostlen) == NULL)
        return -1;
    if (serv && servlen > 0)
        snprintf(serv, servlen, "%u", port);
    return 0;
}

// write_sigrok.c starts sigrok-cli or PulseView; there are no processes to start
int execvp(char const *file, char *const argv[])
{
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

#endif /* ESP_PLATFORM */
