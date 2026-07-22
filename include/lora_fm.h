/** @file
    LoRa demodulation from filtered instantaneous-frequency samples.
*/

#ifndef INCLUDE_LORA_FM_H_
#define INCLUDE_LORA_FM_H_

#include "lora.h"

#include <stddef.h>
#include <stdint.h>

typedef struct lora_fm_demod lora_fm_demod_t;

/// Allocate a streaming filtered-FM LoRa demodulator.
lora_fm_demod_t *lora_fm_demod_create(void);

/// Free a filtered-FM demodulator. A null pointer is accepted.
void lora_fm_demod_free(lora_fm_demod_t *demod);

/// Discard buffered samples and synchronization while retaining workspaces.
void lora_fm_demod_reset(lora_fm_demod_t *demod);

/** Demodulate LoRa from the filtered FM discriminator used by FSK minmax.

    The input scale is -32768..32767 for -Fs/2..Fs/2 and must use the minmax
    default low-pass ratio of 0.2. Set SF, bandwidth, and sync word to zero to
    request analyzer detection. Sample rates and bandwidths must form an
    integer power-of-two oversampling ratio from 2 through 16.

    Calls may stream consecutive blocks; @p sample_offset must continue from
    the preceding block. A discontinuity or sample-rate change resets buffered
    synchronization automatically. Returns the number of CRC-valid packets,
    or -2 for invalid arguments or allocation failure.
*/
int lora_fm_demod_process(lora_fm_demod_t *demod, int16_t const *fm,
        size_t sample_count, uint64_t sample_offset, uint32_t sample_rate,
        unsigned spreading_factor, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets);

#endif /* INCLUDE_LORA_FM_H_ */
