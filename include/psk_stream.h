/** @file
    Streaming raw-IQ support for PSK demodulation.
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