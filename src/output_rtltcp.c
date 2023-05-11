/** @file
    rtl_tcp output for rtl_433 raw data.

    Copyright (C) 2022 Christian Zuckschwerdt

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "output_rtltcp.h"

#include "rtl_433.h"
#include "r_api.h"
#include "r_util.h"
#include "optparse.h"
#include "logger.h"
#include "fatal.h"
#include "compat_pthread.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>

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
    #include <sys/select.h>
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

    static void ws2_perror(const char *str)
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

#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

// MSG_NOSIGNAL is Linux and most BSDs only, not macOS or Windows
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* rtl_tcp server */

// Only available if Threads are enabled.
// Currently serves a maximum of 1 client connection.
// The data backing from the SDR is assumed to be persistent, which is the case
// since we never restart the SDR with different parameters or close it while active.
// Should use shared memory for sendfile() someday.

#ifdef THREADS

typedef struct rtltcp_server {
    struct sockaddr_storage addr;
    socklen_t addr_len;
    SOCKET sock;
    int client_count; ///< number of connected clients
    int control;      ///< are clients allowed to change SDR parameters

    uint8_t const *data_buf; ///< data buffer with most recent data, NULL otherwise
    uint32_t data_len;       ///< data buffer length in bytes, 0 otherwise
    unsigned data_cnt;       ///< data buffer update counter

    pthread_t thread;
    pthread_mutex_t lock; ///< lock for data buffer
    pthread_cond_t cond;  ///< wait for data buffer
    r_cfg_t *cfg;
    struct raw_output *output;
} rtltcp_server_t;

static ssize_t send_all(int sockfd, void const *buf, size_t len, int flags)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t ret = send(sockfd, (uint8_t *)buf + sent, len - sent, flags);
        if (ret < 0)
            return ret;
        sent += (size_t)ret;
    }
    return sent;
}

static void send_header(SOCKET sock)
{
    uint8_t msg[] = {'R', 'T', 'L', '0', 0, 0, 0, 0, 0, 0, 0, 0};
    send_all(sock, msg, sizeof(msg), MSG_NOSIGNAL); // ignore SIGPIPE
}

#define RTLTCP_SET_FREQ 0x01
#define RTLTCP_SET_SAMPLE_RATE 0x02
#define RTLTCP_SET_GAIN_MODE 0x03
#define RTLTCP_SET_GAIN 0x04
#define RTLTCP_SET_FREQ_CORRECTION 0x05
#define RTLTCP_SET_IF_TUNER_GAIN 0x06
#define RTLTCP_SET_TEST_MODE 0x07
#define RTLTCP_SET_AGC_MODE 0x08
#define RTLTCP_SET_DIRECT_SAMPLING 0x09
#define RTLTCP_SET_OFFSET_TUNING 0x0a
#define RTLTCP_SET_RTL_XTAL 0x0b
#define RTLTCP_SET_TUNER_XTAL 0x0c
#define RTLTCP_SET_TUNER_GAIN_BY_ID 0x0d
#define RTLTCP_SET_BIAS_TEE 0x0e

/*
E.g. initialization from Gqrx:
- RTLTCP_SET_GAIN_MODE  with 1
- RTLTCP_SET_AGC_MODE   with 0
- RTLTCP_SET_DIRECT_SAMPLING  with 0
- RTLTCP_SET_OFFSET_TUNING  with 0
- RTLTCP_SET_BIAS_TEE  with 0
- RTLTCP_SET_SAMPLE_RATE  with 250000
- RTLTCP_SET_FREQ  with 52000000
- RTLTCP_SET_GAIN  with 0
- RTLTCP_SET_GAIN  with 0
- RTLTCP_SET_FREQ  with 433968000
*/

static int parse_command(r_cfg_t *cfg, int control, uint8_t const *buf, int len)
{
    UNUSED(cfg);

    if (len < 5)
        return 0;
    int cmd = buf[0];
    unsigned arg = (unsigned)buf[1] << 24 | buf[2] << 16 | buf[3] << 8 | buf[4];
    // print_logf(LOG_TRACE, "rtl_tcp", "CMD: %d with %u (%d) %02x %02x %02x %02x", cmd, arg, (int)arg, buf[1], buf[2], buf[3], buf[4]);
    len -= 5;

    switch (cmd) {
    case RTLTCP_SET_FREQ:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_FREQ with %u", arg);
        if (control)
            set_center_freq(cfg, arg);
        break;
    case RTLTCP_SET_SAMPLE_RATE:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_SAMPLE_RATE with %u", arg);
        if (control)
            set_sample_rate(cfg, arg);
        break;
    case RTLTCP_SET_GAIN_MODE:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_GAIN_MODE with %u", arg);
        // if (control)
        // if (arg == 0 /* =auto */) sdr_set_auto_gain(dev, 0);
        break;
    case RTLTCP_SET_GAIN:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_GAIN with %u", arg);
        // if (control)
        // sdr_set_tuner_gain(dev, char const *gain_str, 0)
        break;
    case RTLTCP_SET_FREQ_CORRECTION:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_FREQ_CORRECTION with %u", arg);
        if (control)
            set_freq_correction(cfg, (int)arg);
        break;
    case RTLTCP_SET_IF_TUNER_GAIN:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_IF_TUNER_GAIN with %u", arg);
        break;
    case RTLTCP_SET_TEST_MODE:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_TEST_MODE with %u", arg);
        break;
    case RTLTCP_SET_AGC_MODE:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_AGC_MODE with %u", arg);
        // ...
        break;
    case RTLTCP_SET_DIRECT_SAMPLING:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_DIRECT_SAMPLING with %u", arg);
        break;
    case RTLTCP_SET_OFFSET_TUNING:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_OFFSET_TUNING with %u", arg);
        break;
    case RTLTCP_SET_RTL_XTAL:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_RTL_XTAL with %u", arg);
        break;
    case RTLTCP_SET_TUNER_XTAL:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_TUNER_XTAL with %u", arg);
        break;
    case RTLTCP_SET_TUNER_GAIN_BY_ID:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_TUNER_GAIN_BY_ID with %u", arg);
        break;
    case RTLTCP_SET_BIAS_TEE:
        print_logf(LOG_DEBUG, "rtl_tcp", "received command SET_BIAS_TEE with %u", arg);
        break;
    default:
        print_logf(LOG_WARNING, "rtl_tcp", "received unknown command %d with %u", cmd, arg);
        break;
    }

    return 5;
}

// event handler to broadcast to all our sockets
static void rtltcp_broadcast_send(rtltcp_server_t *srv, uint8_t const *data, uint32_t len)
{
    // print_logf(LOG_TRACE, __func__, "%d byte frame", len);
    pthread_mutex_lock(&srv->lock);

    // update the data buffer reference
    srv->data_buf = data;
    srv->data_len = len;
    srv->data_cnt += 1;

    pthread_mutex_unlock(&srv->lock);
    pthread_cond_signal(&srv->cond);
    // perhaps broadcast if we want to support multiple clients
    //int pthread_cond_broadcast(&srv->cond);
}

static THREAD_RETURN THREAD_CALL accept_thread(void *arg)
{
    rtltcp_server_t *srv = arg;

    // Start listening for clients, waits for an incoming connection
    listen(srv->sock, 1);
    // print_log(LOG_DEBUG, "rtl_tcp", "rtl_tcp listening...");

    for (;;) {
        // Accept actual connection from the client
        struct sockaddr_storage addr = {0};
        unsigned addr_len = sizeof(addr);
        int sock = accept(srv->sock, (struct sockaddr *)&addr, &addr_len);

        // TODO: ignore ECONNABORTED (Software caused connection abort)
        if (sock < 0) {
            perror("ERROR on accept");
            continue;
        }

        // Prevent SIGPIPE per file descriptor, supported on MacOS and most BSDs
#ifdef SO_NOSIGPIPE
        int opt = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt)) == -1) {
            perror("setsockopt");
            continue;
        }
#endif

        char host[INET6_ADDRSTRLEN] = {0};
        char port[NI_MAXSERV]       = {0};

        int err = getnameinfo((struct sockaddr *)&addr, addr_len,
                host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
        if (err != 0) {
            print_logf(LOG_ERROR, __func__, "failed to convert address to string (code=%d)", err);
            continue;
        }
        print_logf(LOG_NOTICE, "rtl_tcp", "client connected from %s port %s", host, port);

        pthread_mutex_lock(&srv->lock);
        srv->client_count += 1;
        unsigned prev_cnt = srv->data_cnt + 9; // data sent in previous loop, random value to get the current buffer
        pthread_mutex_unlock(&srv->lock);

        send_header(sock);

        // Client loop
        for (;;) {
            // Read available commands
            int abort = 0;
            for (;;) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(sock, &fds);
                struct timeval timeout = {0};

                int ready = select(sock + 1, &fds, NULL, NULL, &timeout);
                if (ready <= 0)
                    break;

                uint8_t buf[128] = {0};
                ssize_t len = recv(sock, buf, sizeof(buf), 0);
                //print_logf(LOG_TRACE, "rtl_tcp", "recv %zd bytes (%d)", len, ready);
                if (len <= 0) {
                    abort = 1;
                    break;
                }
                int pos = 0;
                while (pos + 5 <= len) {
                    pos += parse_command(srv->cfg, srv->control, & buf[pos], (int)len - pos);
                }
            }
            if (abort) {
                break;
            }

            // Wait for send buffer to clear
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sock, &fds);
            struct timeval timeout = {.tv_usec = 100000}; // Wait at most 100 ms

            int ready = select(sock + 1, NULL, &fds, NULL, &timeout);
            if (ready <= 0) {
                print_log(LOG_ERROR, "rtl_tcp", "send not ready for write?");
                break; // Cancel the connection on network problems
            }

            // Wait for next frame
            pthread_mutex_lock(&srv->lock);
            while (srv->data_cnt == prev_cnt || srv->data_buf == NULL)
                pthread_cond_wait(&srv->cond, &srv->lock);
            // Maybe timeout to check recv()
            // pthread_cond_timedwait(&srv->cond, &srv->lock, const struct timespec *abstime);

            // Get data buffer reference
            void const *data = srv->data_buf;
            int data_len     = srv->data_len;
            prev_cnt         = srv->data_cnt;

            pthread_mutex_unlock(&srv->lock);

            // Send frame
            send_all(sock, data, data_len, MSG_NOSIGNAL); // ignore SIGPIPE
        }

        pthread_mutex_lock(&srv->lock);
        srv->client_count -= 1;
        pthread_mutex_unlock(&srv->lock);

        print_logf(LOG_NOTICE, "rtl_tcp", "client disconnected from %s port %s", host, port);
        closesocket(sock);
    }
    return 0;
}

static int rtltcp_server_start(rtltcp_server_t *srv, char const *host, char const *port, r_cfg_t *cfg, struct raw_output *output)
{
    if (!host || !port)
        return -1;

    struct addrinfo hints, *res, *res0;
    int error;
    SOCKET sock;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_ADDRCONFIG;
    error             = getaddrinfo(host, port, &hints, &res0);
    if (error) {
        print_log(LOG_ERROR, __func__, gai_strerror(error));
        return -1;
    }
    sock = INVALID_SOCKET;
    for (res = res0; res; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock >= 0) {
            srv->sock = sock;
            memset(&srv->addr, 0, sizeof(srv->addr));
            memcpy(&srv->addr, res->ai_addr, res->ai_addrlen);
            srv->addr_len = res->ai_addrlen;
            break; // success
        }
    }
    freeaddrinfo(res0);
    if (sock == INVALID_SOCKET) {
        perror("socket");
        return -1;
    }

    if (bind(sock, (struct sockaddr *)&srv->addr, srv->addr_len) < 0) {
        perror("error on binding");
        return -1;
    }

    srv->cfg     = cfg;
    srv->output  = output;

    char address[INET6_ADDRSTRLEN] = {0};
    char portstr[NI_MAXSERV] = {0};

    int err = getnameinfo((struct sockaddr *)&srv->addr, srv->addr_len,
            address, sizeof(address), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
    if (err != 0) {
        print_logf(LOG_ERROR, __func__, "failed to convert address to string (code=%d)", err);
        return -1;
    }
    print_logf(LOG_CRITICAL, "rtl_tcp server", "Serving rtl_tcp on address %s %s", address, portstr);

    pthread_mutex_init(&srv->lock, NULL);
    pthread_cond_init(&srv->cond, NULL);

#ifndef _WIN32
    // Block all signals from the worker thread
    sigset_t sigset;
    sigset_t oldset;
    sigfillset(&sigset);
    pthread_sigmask(SIG_SETMASK, &sigset, &oldset);
#endif
    int r = pthread_create(&srv->thread, NULL, accept_thread, srv);
#ifndef _WIN32
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
#endif
    if (r) {
        fprintf(stderr, "%s: error in pthread_create, rc: %d\n", __func__, r);
    }

    return r;
}

static int rtltcp_server_stop(rtltcp_server_t *srv)
{
    if (!srv)
        return 0;

    print_logf(LOG_NOTICE, "rtl_tcp server", "Stopping rtl_tcp server...");

    // thread is likely blocking in accept, recv, or send
    int r = pthread_cancel(srv->thread);
    if (r) {
        fprintf(stderr, "%s: error in pthread_cancel, rc: %d\n", __func__, r);
    }
    pthread_mutex_destroy(&srv->lock);
    pthread_cond_destroy(&srv->cond);

    srv->client_count = 0;

    // close server socket
    int ret = 0;
    if (srv->sock != INVALID_SOCKET) {
        ret = closesocket(srv->sock);
        srv->sock = INVALID_SOCKET;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return ret;
}

/* rtl_tcp raw output */

typedef struct raw_output_rtltcp {
    struct raw_output output;
    rtltcp_server_t server;
} raw_output_rtltcp_t;

static void raw_output_rtltcp_frame(raw_output_t *output, uint8_t const *data, uint32_t len)
{
    raw_output_rtltcp_t *rtltcp = (raw_output_rtltcp_t *)output;

    rtltcp_broadcast_send(&rtltcp->server, data, len);
}

static void raw_output_rtltcp_free(raw_output_t *output)
{
    raw_output_rtltcp_t *rtltcp = (raw_output_rtltcp_t *)output;

    if (!rtltcp)
        return;

    rtltcp_server_stop(&rtltcp->server);

    free(rtltcp);
}

struct raw_output *raw_output_rtltcp_create(const char *host, const char *port, char const *opts, r_cfg_t *cfg)
{
    raw_output_rtltcp_t *rtltcp = calloc(1, sizeof(raw_output_rtltcp_t));
    if (!rtltcp) {
        WARN_CALLOC("raw_output_rtltcp_create()");
        return NULL; // NOTE: returns NULL on alloc failure.
    }
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        perror("WSAStartup()");
        free(rtltcp);
        return NULL;
    }
#endif

    // If clients allowed to change SDR parameters
    if (opts && !strcasecmp(opts, "control"))
        rtltcp->server.control = 1;
    else if (opts && *opts) {
        print_logf(LOG_FATAL, __func__, "Invalid \"%s\" option.", opts);
        exit(1);
    }

    rtltcp->output.output_frame  = raw_output_rtltcp_frame;
    rtltcp->output.output_free   = raw_output_rtltcp_free;

    int ret = rtltcp_server_start(&rtltcp->server, host, port, cfg, &rtltcp->output);
    if (ret != 0) {
        exit(1);
    }

    return &rtltcp->output;
}

#else

struct raw_output *raw_output_rtltcp_create(const char *host, const char *port, r_cfg_t *cfg)
{
    UNUSED(host);
    UNUSED(port);
    UNUSED(cfg);
    print_log(LOG_ERROR, "rtl_tcp server", "rtl_tcp output not available in this build!");
    return NULL;
}

#endif
