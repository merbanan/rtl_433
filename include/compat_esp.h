/** @file
    ESP-IDF (newlib and lwIP) compatibility.

    topic: POSIX and netdb functions and definitions
    issue: newlib and lwIP lack a few that the network outputs and the config file probing use
    solution: provide them on ESP-IDF, see compat_esp.c
*/

#ifndef INCLUDE_COMPAT_ESP_H_
#define INCLUDE_COMPAT_ESP_H_

#ifdef ESP_PLATFORM

#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 1
#endif
#ifndef NI_NUMERICSERV
#define NI_NUMERICSERV 2
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46 // lwIP only defines it with IPv6 enabled
#endif
#ifndef _POSIX_HOST_NAME_MAX
#define _POSIX_HOST_NAME_MAX 128
#endif

/// lwIP's getaddrinfo() returns EAI_* codes but has no gai_strerror().
char const *gai_strerror(int ecode);

#endif /* ESP_PLATFORM */

#endif /* INCLUDE_COMPAT_ESP_H_ */
