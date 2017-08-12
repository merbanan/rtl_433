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
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#ifndef __MINGW32__
#include "getopt/getopt.h"
#else
#include <getopt.h>
#endif
#endif

#define DEFAULT_SAMPLE_RATE     250000
#define DEFAULT_FREQUENCY       433920000
#define DEFAULT_HOP_TIME        (60*10)
#define DEFAULT_HOP_EVENTS      2
#define DEFAULT_ASYNC_BUF_NUMBER    0 // Force use of default value (was : 32)
#define DEFAULT_BUF_LENGTH      (16 * 16384)

/*
 * Theoretical high level at I/Q saturation is 128x128 = 16384 (above is ripple)
 * 0 = automatic adaptive level limit, else fixed level limit
 * 8000 = previous fixed default
 */
#define DEFAULT_LEVEL_LIMIT     0

#define MINIMAL_BUF_LENGTH      512
#define MAXIMAL_BUF_LENGTH      (256 * 16384)
#define MAX_PROTOCOLS           91
#define SIGNAL_GRABBER_BUFFER   (12 * DEFAULT_BUF_LENGTH)

/* Supported modulation types */
#define	OOK_PULSE_MANCHESTER_ZEROBIT	3	// Manchester encoding. Hardcoded zerobit. Rising Edge = 0, Falling edge = 1
#define	OOK_PULSE_PCM_RZ		4			// Pulse Code Modulation with Return-to-Zero encoding, Pulse = 0, No pulse = 1
#define	OOK_PULSE_PPM_RAW		5			// Pulse Position Modulation. No startbit removal. Short gap = 0, Long = 1
#define	OOK_PULSE_PWM_PRECISE	6			// Pulse Width Modulation with precise timing parameters
#define	OOK_PULSE_PWM_RAW		7			// Pulse Width Modulation. Short pulses = 1, Long = 0
#define	OOK_PULSE_PWM_TERNARY	8			// Pulse Width Modulation with three widths: Sync, 0, 1. Sync determined by argument
#define	OOK_PULSE_CLOCK_BITS	9			// Level shift within the clock cycle.
#define	OOK_PULSE_PWM_OSV1		10			// Pulse Width Modulation. Oregon Scientific v1

#define	FSK_DEMOD_MIN_VAL		16			// Dummy. FSK demodulation must start at this value
#define	FSK_PULSE_PCM			16			// FSK, Pulse Code Modulation
#define	FSK_PULSE_PWM_RAW		17			// FSK, Pulse Width Modulation. Short pulses = 1, Long = 0
#define FSK_PULSE_MANCHESTER_ZEROBIT 18		// FSK, Manchester encoding

extern int debug_output;
extern float sample_file_pos;

struct protocol_state {
    int (*callback)(bitbuffer_t *bitbuffer);

   // Bits state (for old sample based decoders)
    bitbuffer_t bits;

    unsigned int modulation;

    /* pwm limits (provided by driver in µs and converted to samples) */
    float short_limit;
    float long_limit;
    float reset_limit;
    char *name;
    unsigned long demod_arg;
};

void data_acquired_handler(data_t *data);

#endif /* INCLUDE_RTL_433_H_ */
