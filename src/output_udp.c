/** @file
    UDP syslog output for rtl_433 events.

    Copyright (C) 2021 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "output_udp.h"

#include "data.h"
#include "abuf.h"
#include "r_util.h"
#include "logger.h"
#include "fatal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <limits.h>
// gethostname() needs _XOPEN_SOURCE 500 on unistd.h
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
    #if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
    #undef _WIN32_WINNT
    #define _WIN32_WINNT 0x0600   /* Needed to pull in 'struct sockaddr_storage' */
    #endif

    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <netinet/in.h>

    #define SOCKET          int
    #define INVALID_SOCKET  (-1)
    #define closesocket(x)  close(x)
#endif

#include <time.h>

#ifdef _WIN32
    #define _POSIX_HOST_NAME_MAX  128
    #define perror(str)           ws2_perror(str)

    static void ws2_perror (const char *str)
    {
        if (str && *str)
            fprintf(stderr, "%s: ", str);
        fprintf(stderr, "Winsock error %d.\n", WSAGetLastError());
    }
#endif
#ifdef ESP32
    #include <tcpip_adapter.h>
    #define _POSIX_HOST_NAME_MAX 128
    #define gai_strerror strerror
#endif

/* Datagram (UDP) client */

typedef struct {
    struct sockaddr_storage addr;
    socklen_t addr_len;
    SOCKET sock;
} datagram_client_t;

static int datagram_client_open(datagram_client_t *client, const char *host, const char *port)
{
    if (!host || !port)
        return -1;

    struct addrinfo hints, *res, *res0;
    int    error;
    SOCKET sock;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_ADDRCONFIG;
    error = getaddrinfo(host, port, &hints, &res0);
    if (error) {
        print_log(LOG_ERROR, __func__, gai_strerror(error));
        return -1;
    }
    sock = INVALID_SOCKET;
    for (res = res0; res; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock >= 0) {
            client->sock = sock;
            memset(&client->addr, 0, sizeof(client->addr));
            memcpy(&client->addr, res->ai_addr, res->ai_addrlen);
            client->addr_len = res->ai_addrlen;
            break; // success
        }
    }
    freeaddrinfo(res0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return -1;
    }

    //int broadcast = 1;
    //int ret = setsockopt(client->sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    return 0;
}

static void datagram_client_close(datagram_client_t *client)
{
    if (!client)
        return;

    if (client->sock != INVALID_SOCKET) {
        closesocket(client->sock);
        client->sock = INVALID_SOCKET;
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

static void datagram_client_send(datagram_client_t *client, const char *message, size_t message_len)
{
    int r =  sendto(client->sock, message, message_len, 0, (struct sockaddr *)&client->addr, client->addr_len);
    if (r == -1) {
        perror("sendto");
    }
}

/* Syslog UDP printer, RFC 5424 (IETF-syslog protocol) */

typedef struct {
    struct data_output output;
    datagram_client_t client;
    int pri;
    char hostname[_POSIX_HOST_NAME_MAX + 1];
} data_output_syslog_t;

static void R_API_CALLCONV data_output_syslog_print(data_output_t *output, data_t *data)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    // we expect a normal message around 500 bytes
    // full stats report would be 12k and we want a max of MTU anyway
    char message[1024];
    abuf_t msg = {0};
    abuf_init(&msg, message, sizeof(message));

    time_t now;
    struct tm tm_info;
    time(&now);
#ifdef _WIN32
    gmtime_s(&tm_info, &now);
#else
    gmtime_r(&now, &tm_info);
#endif
    char timestamp[21];
    strftime(timestamp, 21, "%Y-%m-%dT%H:%M:%SZ", &tm_info);

    abuf_printf(&msg, "<%d>1 %s %s rtl_433 - - - ", syslog->pri, timestamp, syslog->hostname);

    msg.tail += data_print_jsons(data, msg.tail, msg.left);
    if (msg.tail >= msg.head + sizeof(message))
        return; // abort on overflow, we don't actually want to send more than fits the MTU

    size_t abuf_len = msg.tail - msg.head;
    datagram_client_send(&syslog->client, message, abuf_len);
}

static void R_API_CALLCONV data_output_syslog_free(data_output_t *output)
{
    data_output_syslog_t *syslog = (data_output_syslog_t *)output;

    if (!syslog)
        return;

    datagram_client_close(&syslog->client);

    free(syslog);
}

struct data_output *data_output_syslog_create(int log_level, const char *host, const char *port)
{
    data_output_syslog_t *syslog = calloc(1, sizeof(data_output_syslog_t));
    if (!syslog) {
        WARN_CALLOC("data_output_syslog_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2,2),&wsa) != 0) {
        perror("WSAStartup()");
        free(syslog);
        return NULL;
    }
#endif

    syslog->output.log_level    = log_level;
    syslog->output.output_print = data_output_syslog_print;
    syslog->output.output_free  = data_output_syslog_free;
    // Severity 5 "Notice", Facility 20 "local use 4"
    syslog->pri = 20 * 8 + 5;
    #ifdef ESP32
    const char* adapter_hostname = NULL;
    tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &adapter_hostname);
    if (adapter_hostname) {
        memcpy(syslog->hostname, adapter_hostname, _POSIX_HOST_NAME_MAX);
    }
    else {
        syslog->hostname[0] = '\0';
    }
    #else
    gethostname(syslog->hostname, _POSIX_HOST_NAME_MAX + 1);
    #endif
    syslog->hostname[_POSIX_HOST_NAME_MAX] = '\0';
    datagram_client_open(&syslog->client, host, port);

    return &syslog->output;
}
