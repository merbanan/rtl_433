/** @file
    Integer mixed-radix FFT helpers.

    The forward transform uses radix-4 stages with an initial radix-2 stage
    for odd orders. It does not scale between stages, so callers must bound
    input amplitudes to keep the accumulated values within int32_t.
*/

#ifndef INCLUDE_FFT_H_
#define INCLUDE_FFT_H_

#include <stdint.h>

typedef struct {
    int32_t re;
    int32_t im;
} fft_fixed_sample_t;

typedef struct {
    int16_t re;
    int16_t im;
} fft_fixed_twiddle_t;

/** Return log2(length), or zero when length is not a power of two >= 2. */
unsigned fft_fixed_order(unsigned length);

/** Return the input index for a mixed-radix digit-reversed position.

    Before calling fft_fixed_transform(), arrange natural-order input as:

        data[position] = input[fft_fixed_mixed_reverse(position, order)]
*/
unsigned fft_fixed_mixed_reverse(unsigned position, unsigned order);

/** Perform an unscaled forward FFT in place.

    @p data must already be in mixed-radix digit-reversed order. The twiddle
    table contains Q15 values for exp(-j * 2*pi*k/length), indexed by k, and
    must contain @p length entries. Length must be a power of two >= 2.
*/
void fft_fixed_transform(fft_fixed_sample_t *data, unsigned length,
        fft_fixed_twiddle_t const *twiddle);

/** Perform a forward FFT with one-bit/radix scaling at each stage.

    Input must be in mixed-radix digit-reversed order and components must not
    exceed 23170. Output is in natural order and scaled by 1 / length.
*/
void fft_fixed_transform_scaled(fft_fixed_sample_t *data, unsigned length,
        fft_fixed_twiddle_t const *twiddle);

#endif /* INCLUDE_FFT_H_ */
