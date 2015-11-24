#ifndef INCLUDE_RTL_433_H_
#define INCLUDE_RTL_433_H_

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#include "rtl_433_devices.h"
#include "bitbuffer.h"
#include "data.h"

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
#define DEFAULT_LEVEL_LIMIT     8000		// Theoretical high level at I/Q saturation is 128x128 = 16384 (above is ripple)
#define MINIMAL_BUF_LENGTH      512
#define MAXIMAL_BUF_LENGTH      (256 * 16384)
#define MAX_PROTOCOLS           40
#define SIGNAL_GRABBER_BUFFER   (12 * DEFAULT_BUF_LENGTH)

/* Supported modulation types */
#define	OOK_PWM_D				1			// (Deprecated) Pulses are of the same length, the distance varies (PPM)
#define	OOK_PWM_P				2			// (Deprecated) The length of the pulses varies
#define	OOK_PULSE_MANCHESTER_ZEROBIT	3	// Manchester encoding. Hardcoded zerobit. Rising Edge = 0, Falling edge = 1
#define	OOK_PULSE_PCM_RZ		4			// Pulse Code Modulation with Return-to-Zero encoding, Pulse = 0, No pulse = 1
#define	OOK_PULSE_PPM_RAW		5			// Pulse Position Modulation. No startbit removal. Short gap = 0, Long = 1
#define	OOK_PULSE_PWM_PRECISE	6			// Pulse Width Modulation with precise timing parameters
#define	OOK_PULSE_PWM_RAW		7			// Pulse Width Modulation. Short pulses = 1, Long = 0
#define	OOK_PULSE_PWM_TERNARY	8			// Pulse Width Modulation with three widths: Sync, 0, 1. Sync determined by argument
#define	OOK_PULSE_CLOCK_BITS		9			// Level shift within the clock cycle.

#define	FSK_DEMOD_MIN_VAL		16			// Dummy. FSK demodulation must start at this value
#define	FSK_PULSE_PCM			16			// FSK, Pulse Code Modulation
#define	FSK_PULSE_PWM_RAW		17			// FSK, Pulse Width Modulation. Short pulses = 1, Long = 0

extern int debug_output;

struct protocol_state {
    int (*callback)(bitbuffer_t *bitbuffer);

   // Bits state (for old sample based decoders)
    bitbuffer_t bits;

    unsigned int modulation;

    /* demod state */
    unsigned int pulse_length;
    int pulse_count;
    int pulse_distance;
    unsigned int sample_counter;
    int start_c;

    int packet_present;
    int pulse_start;
    int real_bits;
    int start_bit;
    /* pwm limits */
    unsigned int short_limit;
    unsigned int long_limit;
    unsigned int reset_limit;
    char *name;
    unsigned long demod_arg;
};

void data_acquired_handler(data_t *data);

#endif /* INCLUDE_RTL_433_H_ */
