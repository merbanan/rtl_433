/** @file
    Streaming raw-IQ support for PSK demodulation.

    Copyright (C) 2026 Kim Bloxsom

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_PSK_STREAM_H_
#define INCLUDE_PSK_STREAM_H_

#include <stddef.h>
#include <stdint.h>

#include "rtl_433.h"

void psk_stream_reset(r_cfg_t *cfg);

int psk_stream_push(
        r_cfg_t *cfg,
        uint8_t const *iq_buf,
        size_t sample_count);

#endif /* INCLUDE_PSK_STREAM_H_ */
