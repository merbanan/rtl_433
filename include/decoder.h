/** @file
    Meta include for all decoders.
*/

#ifndef INCLUDE_DECODER_H_
#define INCLUDE_DECODER_H_

#include <string.h>
#include "r_device.h"
#include "bitbuffer.h"
#include "data.h"
#include "util.h"
#include "decoder_util.h"

/** Supported modulation types. */
enum modulation_types {
    OOK_PULSE_MANCHESTER_ZEROBIT =  3,  ///< Manchester encoding. Hardcoded zerobit. Rising Edge = 0, Falling edge = 1.
    OOK_PULSE_PCM_RZ =              4,  ///< Pulse Code Modulation with Return-to-Zero encoding, Pulse = 0, No pulse = 1.
    OOK_PULSE_PPM =                 5,  ///< Pulse Position Modulation. Short gap = 0, Long = 1.
    OOK_PULSE_PWM =                 6,  ///< Pulse Width Modulation with precise timing parameters.
    OOK_PULSE_PIWM_RAW =            8,  ///< Level shift for each bit. Short interval = 1, Long = 0.
    OOK_PULSE_PIWM_DC =             11, ///< Level shift for each bit. Short interval = 1, Long = 0.
    OOK_PULSE_DMC =                 9,  ///< Level shift within the clock cycle.
    OOK_PULSE_PWM_OSV1 =            10, ///< Pulse Width Modulation. Oregon Scientific v1.

    FSK_DEMOD_MIN_VAL =             16, ///< Dummy. FSK demodulation must start at this value.
    FSK_PULSE_PCM =                 16, ///< FSK, Pulse Code Modulation.
    FSK_PULSE_PWM =                 17, ///< FSK, Pulse Width Modulation. Short pulses = 1, Long = 0.
    FSK_PULSE_MANCHESTER_ZEROBIT =  18, ///< FSK, Manchester encoding.
};

#endif /* INCLUDE_DECODER_H_ */
