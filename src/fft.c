/** @file
    Unscaled fixed-point mixed-radix forward FFT.
*/

#include "fft.h"

unsigned fft_fixed_order(unsigned length)
{
    if (length < 2 || (length & (length - 1))) {
        return 0;
    }
    unsigned order = 0;
    while (length > 1) {
        length >>= 1;
        order += 1;
    }
    return order;
}

unsigned fft_fixed_mixed_reverse(unsigned position, unsigned order)
{
    unsigned reversed = 0;
    if (order & 1) {
        reversed = position & 1;
        position >>= 1;
        order -= 1;
    }
    for (unsigned i = 0; i < order / 2; ++i) {
        reversed = reversed * 4 + (position & 3);
        position >>= 2;
    }
    return reversed;
}

static int32_t rounded_shift(int32_t value, unsigned shift)
{
    int32_t const half = 1 << (shift - 1);
    return (value + half) >> shift;
}

void fft_fixed_transform(fft_fixed_sample_t *data, unsigned length,
        fft_fixed_twiddle_t const *twiddle)
{
    unsigned const order = fft_fixed_order(length);
    if (!order || !data || !twiddle) {
        return;
    }

    if (order & 1) {
        for (unsigned base = 0; base < length; base += 2) {
            fft_fixed_sample_t const a = data[base];
            fft_fixed_sample_t const b = data[base + 1];
            data[base].re = a.re + b.re;
            data[base].im = a.im + b.im;
            data[base + 1].re = a.re - b.re;
            data[base + 1].im = a.im - b.im;
        }
    }

    for (unsigned size = (order & 1) ? 8 : 4; size <= length; size *= 4) {
        unsigned const quarter = size / 4;
        unsigned const twiddle_step = length / size;
        for (unsigned base = 0; base < length; base += size) {
            for (unsigned j = 0; j < quarter; ++j) {
                fft_fixed_sample_t a = data[base + j];
                fft_fixed_sample_t b = data[base + j + quarter];
                fft_fixed_sample_t c = data[base + j + 2 * quarter];
                fft_fixed_sample_t d = data[base + j + 3 * quarter];
                if (j) {
                    fft_fixed_twiddle_t const wb = twiddle[j * twiddle_step];
                    fft_fixed_twiddle_t const wc = twiddle[2 * j * twiddle_step];
                    fft_fixed_twiddle_t const wd = twiddle[3 * j * twiddle_step];
                    fft_fixed_sample_t const b0 = b;
                    fft_fixed_sample_t const c0 = c;
                    fft_fixed_sample_t const d0 = d;
                    b.re = (int32_t)(((int64_t)b0.re * wb.re - (int64_t)b0.im * wb.im) >> 15);
                    b.im = (int32_t)(((int64_t)b0.re * wb.im + (int64_t)b0.im * wb.re) >> 15);
                    c.re = (int32_t)(((int64_t)c0.re * wc.re - (int64_t)c0.im * wc.im) >> 15);
                    c.im = (int32_t)(((int64_t)c0.re * wc.im + (int64_t)c0.im * wc.re) >> 15);
                    d.re = (int32_t)(((int64_t)d0.re * wd.re - (int64_t)d0.im * wd.im) >> 15);
                    d.im = (int32_t)(((int64_t)d0.re * wd.im + (int64_t)d0.im * wd.re) >> 15);
                }

                int32_t const sum_ac_re = a.re + c.re;
                int32_t const sum_ac_im = a.im + c.im;
                int32_t const diff_ac_re = a.re - c.re;
                int32_t const diff_ac_im = a.im - c.im;
                int32_t const sum_bd_re = b.re + d.re;
                int32_t const sum_bd_im = b.im + d.im;
                int32_t const diff_bd_re = b.re - d.re;
                int32_t const diff_bd_im = b.im - d.im;
                data[base + j].re = sum_ac_re + sum_bd_re;
                data[base + j].im = sum_ac_im + sum_bd_im;
                data[base + j + quarter].re = diff_ac_re + diff_bd_im;
                data[base + j + quarter].im = diff_ac_im - diff_bd_re;
                data[base + j + 2 * quarter].re = sum_ac_re - sum_bd_re;
                data[base + j + 2 * quarter].im = sum_ac_im - sum_bd_im;
                data[base + j + 3 * quarter].re = diff_ac_re - diff_bd_im;
                data[base + j + 3 * quarter].im = diff_ac_im + diff_bd_re;
            }
        }
    }
}

void fft_fixed_transform_scaled(fft_fixed_sample_t *data, unsigned length,
        fft_fixed_twiddle_t const *twiddle)
{
    unsigned const order = fft_fixed_order(length);
    if (!order || !data || !twiddle) {
        return;
    }

    if (order & 1) {
        for (unsigned base = 0; base < length; base += 2) {
            fft_fixed_sample_t const a = data[base];
            fft_fixed_sample_t const b = data[base + 1];
            data[base].re = rounded_shift(a.re + b.re, 1);
            data[base].im = rounded_shift(a.im + b.im, 1);
            data[base + 1].re = rounded_shift(a.re - b.re, 1);
            data[base + 1].im = rounded_shift(a.im - b.im, 1);
        }
    }

    for (unsigned size = (order & 1) ? 8 : 4; size <= length; size *= 4) {
        unsigned const quarter = size / 4;
        unsigned const twiddle_step = length / size;
        for (unsigned base = 0; base < length; base += size) {
            for (unsigned j = 0; j < quarter; ++j) {
                fft_fixed_sample_t a = data[base + j];
                fft_fixed_sample_t b = data[base + j + quarter];
                fft_fixed_sample_t c = data[base + j + 2 * quarter];
                fft_fixed_sample_t d = data[base + j + 3 * quarter];
                if (j) {
                    fft_fixed_twiddle_t const wb = twiddle[j * twiddle_step];
                    fft_fixed_twiddle_t const wc = twiddle[2 * j * twiddle_step];
                    fft_fixed_twiddle_t const wd = twiddle[3 * j * twiddle_step];
                    fft_fixed_sample_t const b0 = b;
                    fft_fixed_sample_t const c0 = c;
                    fft_fixed_sample_t const d0 = d;
                    b.re = rounded_shift(b0.re * wb.re - b0.im * wb.im, 15);
                    b.im = rounded_shift(b0.re * wb.im + b0.im * wb.re, 15);
                    c.re = rounded_shift(c0.re * wc.re - c0.im * wc.im, 15);
                    c.im = rounded_shift(c0.re * wc.im + c0.im * wc.re, 15);
                    d.re = rounded_shift(d0.re * wd.re - d0.im * wd.im, 15);
                    d.im = rounded_shift(d0.re * wd.im + d0.im * wd.re, 15);
                }

                int32_t const sum_ac_re = a.re + c.re;
                int32_t const sum_ac_im = a.im + c.im;
                int32_t const diff_ac_re = a.re - c.re;
                int32_t const diff_ac_im = a.im - c.im;
                int32_t const sum_bd_re = b.re + d.re;
                int32_t const sum_bd_im = b.im + d.im;
                int32_t const diff_bd_re = b.re - d.re;
                int32_t const diff_bd_im = b.im - d.im;
                data[base + j].re = rounded_shift(sum_ac_re + sum_bd_re, 2);
                data[base + j].im = rounded_shift(sum_ac_im + sum_bd_im, 2);
                data[base + j + quarter].re = rounded_shift(diff_ac_re + diff_bd_im, 2);
                data[base + j + quarter].im = rounded_shift(diff_ac_im - diff_bd_re, 2);
                data[base + j + 2 * quarter].re = rounded_shift(sum_ac_re - sum_bd_re, 2);
                data[base + j + 2 * quarter].im = rounded_shift(sum_ac_im - sum_bd_im, 2);
                data[base + j + 3 * quarter].re = rounded_shift(diff_ac_re - diff_bd_im, 2);
                data[base + j + 3 * quarter].im = rounded_shift(diff_ac_im + diff_bd_re, 2);
            }
        }
    }
}
