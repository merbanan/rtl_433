#include "fft.h"

#include <stdio.h>
#include <stdlib.h>

static int close_to(int32_t actual, int32_t expected)
{
    return abs(actual - expected) <= 2;
}

int main(void)
{
    fft_fixed_twiddle_t const twiddle4[4] = {
            {32767, 0}, {0, -32767}, {-32767, 0}, {0, 32767},
    };
    fft_fixed_sample_t data4[4] = {
            {1, 0}, {2, 0}, {3, 0}, {4, 0},
    };
    fft_fixed_sample_t const expected4[4] = {
            {10, 0}, {-2, 2}, {-2, 0}, {-2, -2},
    };
    fft_fixed_transform(data4, 4, twiddle4);
    for (unsigned i = 0; i < 4; ++i) {
        if (data4[i].re != expected4[i].re || data4[i].im != expected4[i].im) {
            fprintf(stderr, "fft: radix-4 vector failed at bin %u\n", i);
            return 1;
        }
    }

    fft_fixed_twiddle_t const twiddle8[8] = {
            {32767, 0}, {23170, -23170}, {0, -32767}, {-23170, -23170},
            {-32767, 0}, {-23170, 23170}, {0, 32767}, {23170, 23170},
    };
    fft_fixed_sample_t natural8[8] = {{0}};
    fft_fixed_sample_t data8[8];
    fft_fixed_sample_t const expected8[8] = {
            {1024, 0}, {724, -724}, {0, -1024}, {-724, -724},
            {-1024, 0}, {-724, 724}, {0, 1024}, {724, 724},
    };
    natural8[1].re = 1024;
    for (unsigned i = 0; i < 8; ++i) {
        data8[i] = natural8[fft_fixed_mixed_reverse(i, 3)];
    }
    fft_fixed_transform(data8, 8, twiddle8);
    for (unsigned i = 0; i < 8; ++i) {
        if (!close_to(data8[i].re, expected8[i].re)
                || !close_to(data8[i].im, expected8[i].im)) {
            fprintf(stderr, "fft: mixed-radix vector failed at bin %u\n", i);
            return 1;
        }
    }

    for (unsigned i = 0; i < 8; ++i) {
        data8[i] = natural8[fft_fixed_mixed_reverse(i, 3)];
    }
    fft_fixed_transform_scaled(data8, 8, twiddle8);
    for (unsigned i = 0; i < 8; ++i) {
        if (!close_to(data8[i].re, expected8[i].re / 8)
                || !close_to(data8[i].im, expected8[i].im / 8)) {
            fprintf(stderr, "fft: scaled mixed-radix vector failed at bin %u\n", i);
            return 1;
        }
    }

    fprintf(stderr, "fft: tests passed\n");
    return 0;
}
