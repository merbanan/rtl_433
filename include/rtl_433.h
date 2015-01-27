#ifndef INCLUDE_RTL_433_H_
#define INCLUDE_RTL_433_H_

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#include "getopt/getopt.h"
#endif

#define DEFAULT_SAMPLE_RATE     250000
#define DEFAULT_FREQUENCY       433920000
#define DEFAULT_HOP_TIME        (60*10)
#define DEFAULT_HOP_EVENTS      2
#define DEFAULT_ASYNC_BUF_NUMBER    32
#define DEFAULT_BUF_LENGTH      (16 * 16384)
#define DEFAULT_LEVEL_LIMIT     10000
#define DEFAULT_DECIMATION_LEVEL 0
#define MINIMAL_BUF_LENGTH      512
#define MAXIMAL_BUF_LENGTH      (256 * 16384)
#define FILTER_ORDER            1
#define MAX_PROTOCOLS           15
#define SIGNAL_GRABBER_BUFFER   (12 * DEFAULT_BUF_LENGTH)
#define BITBUF_COLS             34
#define BITBUF_ROWS             50

/* Supported modulation types */
#define     OOK_PWM_D		1   /* Pulses are of the same length, the distance varies (PPM) */
#define     OOK_PWM_P		2   /* The length of the pulses varies */
#define     OOK_MANCHESTER	3	/* Manchester code */
#define     OOK_PWM_RAW	    4   /* Pulse Width Modulation. No startbit removal. Short pulses = 1, Long = 0 */

typedef struct {
    unsigned int    id;
    char            name[256];
    unsigned int    modulation;
    unsigned int    short_limit;
    unsigned int    long_limit;
    unsigned int    reset_limit;
    int     (*json_callback)(uint8_t bits_buffer[BITBUF_ROWS][BITBUF_COLS],int16_t bits_per_row[BITBUF_ROWS]) ;
} r_device;

extern int debug_output;
int debug_callback(uint8_t buffer[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]);

#endif /* INCLUDE_RTL_433_H_ */
