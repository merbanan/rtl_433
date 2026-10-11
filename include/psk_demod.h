/** @file
    PSK candidate detection and differential BPSK demodulation.

    Copyright (C) 2026 Kim Bloxsom

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_PSK_DEMOD_H_
#define INCLUDE_PSK_DEMOD_H_

#include <stddef.h>
#include <stdint.h>

#include "bitbuffer.h"

typedef struct {
    double offset_s;
    double signature_db;
    double carrier_offset_hz;
    double symbol_rate_hint;
} psk_candidate_t;

typedef int (*psk_validate_fn)(
        bitbuffer_t *bitbuffer,
        void *context);

int psk_find_candidate(
        uint8_t const *iq_buf,
        size_t sample_count,
        unsigned sample_size,
        unsigned sample_rate,
        psk_candidate_t *candidate);

int psk_demod_candidate(
        uint8_t const *iq_buf,
        size_t sample_count,
        unsigned sample_size,
        unsigned sample_rate,
        psk_candidate_t const *candidate,
        psk_validate_fn validate_fn,
        void *validate_context,
        bitbuffer_t *bitbuffer);

#endif /* INCLUDE_PSK_DEMOD_H_ */
