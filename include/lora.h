/** @file
    Fixed-point LoRa PHY demodulator.
*/

#ifndef INCLUDE_LORA_H_
#define INCLUDE_LORA_H_

#include <stddef.h>
#include <stdint.h>

#define LORA_MAX_PAYLOAD_LEN 255

typedef struct lora_fft_demod lora_fft_demod_t;

typedef struct {
    uint8_t payload[LORA_MAX_PAYLOAD_LEN];
    unsigned payload_len;
    unsigned spreading_factor;
    unsigned coding_rate;
    unsigned sync_word;
    uint32_t bandwidth;
    uint64_t start_offset;
    uint64_t end_offset;
} lora_packet_t;

typedef enum {
    LORA_SYMBOLS_INVALID = -1,
    LORA_SYMBOLS_INCOMPLETE = 0,
    LORA_SYMBOLS_VALID = 1,
} lora_symbols_result_t;

/// Allocate an IQ/FFT LoRa demodulator state.
lora_fft_demod_t *lora_fft_demod_create(void);

/// Free an IQ/FFT LoRa demodulator state.
void lora_fft_demod_free(lora_fft_demod_t *demod);

/// Clear buffered samples and synchronization state.
void lora_fft_demod_reset(lora_fft_demod_t *demod);

/** Decode an explicit-header LoRa symbol stream.

    This entry point is primarily useful for PHY tests. The payload is returned
    only when both the PHY header checksum and payload CRC are valid.
*/
int lora_decode_symbols(uint16_t const *symbols, unsigned symbol_count,
        unsigned spreading_factor, uint32_t bandwidth, lora_packet_t *packet);

/** Inspect or decode an explicit-header LoRa symbol stream.

    Unlike lora_decode_symbols(), this reports a valid PHY header whose
    declared payload has not arrived yet. On INCOMPLETE or VALID,
    @p expected_symbol_count receives the exact frame length in symbols.
    Both standard and nonstandard low-data-rate optimization settings are
    tried so callers can use the PHY header and CRC to select the mode.
*/
lora_symbols_result_t lora_decode_symbols_ex(uint16_t const *symbols,
        unsigned symbol_count, unsigned spreading_factor, uint32_t bandwidth,
        lora_packet_t *packet, unsigned *expected_symbol_count);

/** Retry a failed CR 4/5 or 4/6 stream using FEC-guided adjacent bins.

    Only symbol changes that reduce the payload Hamming parity syndrome are
    combined. The payload CRC selects a result, and @p max_trials bounds the
    number of parity-clean candidates tested.
*/
int lora_decode_symbols_repaired(uint16_t const *symbols,
        unsigned symbol_count, unsigned spreading_factor, uint32_t bandwidth,
        lora_packet_t *packet, unsigned max_trials);

/** Demodulate LoRa from interleaved unsigned 8-bit IQ samples.

    The fixed-point synchronizer supports SF7 through SF12 and power-of-two
    oversampling ratios from 2 through 16 samples per chip. Set spreading
    factor, bandwidth, and sync word to zero to request analyzer detection.
    rtl_433 live/file demodulation should use exactly 1,000,000 or 2,000,000
    samples per second. A 1,024,000 sample/s capture requires rational
    resampling rather than changing only its declared sample rate.
    Returns the number of CRC-valid packets written to @p packets, or -1 when
    the requested sample-rate/PHY combination is unsupported.
*/
int lora_fft_demod_process_cu8(lora_fft_demod_t *demod, uint8_t const *iq_buf,
        size_t sample_count, uint64_t sample_offset, uint32_t sample_rate,
        unsigned spreading_factor, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets);

/** Demodulate LoRa from interleaved signed 16-bit IQ samples. */
int lora_fft_demod_process_cs16(lora_fft_demod_t *demod, int16_t const *iq_buf,
        size_t sample_count, uint64_t sample_offset, uint32_t sample_rate,
        unsigned spreading_factor, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets);

#endif /* INCLUDE_LORA_H_ */
