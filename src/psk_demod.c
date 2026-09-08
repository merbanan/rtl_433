#include "psk_demod.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    double re;
    double im;
} cpx_t;

static cpx_t c_add(cpx_t a, cpx_t b)
{
    cpx_t r = {a.re + b.re, a.im + b.im};
    return r;
}

static cpx_t c_sub(cpx_t a, cpx_t b)
{
    cpx_t r = {a.re - b.re, a.im - b.im};
    return r;
}

static cpx_t c_mul(cpx_t a, cpx_t b)
{
    cpx_t r = {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
    return r;
}

static cpx_t c_scale(cpx_t a, double s)
{
    cpx_t r = {a.re * s, a.im * s};
    return r;
}

static double c_abs2(cpx_t a)
{
    return a.re * a.re + a.im * a.im;
}

static cpx_t c_expj(double phase)
{
    cpx_t r = {cos(phase), sin(phase)};
    return r;
}

static void fft_inplace(cpx_t *a, size_t n)
{
    size_t i;
    size_t j;
    size_t len;

    for (i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;

        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;

        if (i < j) {
            cpx_t t = a[i];
            a[i] = a[j];
            a[j] = t;
        }
    }

    for (len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / (double)len;
        cpx_t wlen = c_expj(ang);

        for (i = 0; i < n; i += len) {
            cpx_t w = {1.0, 0.0};
            size_t half = len >> 1;

            for (j = 0; j < half; j++) {
                cpx_t u = a[i + j];
                cpx_t v = c_mul(a[i + j + half], w);

                a[i + j] = c_add(u, v);
                a[i + j + half] = c_sub(u, v);
                w = c_mul(w, wlen);
            }
        }
    }
}

static int cmp_double(void const *pa, void const *pb)
{
    double a = *(double const *)pa;
    double b = *(double const *)pb;

    return (a > b) - (a < b);
}

static double median_copy(double const *v, size_t n)
{
    double *tmp;
    double result;

    if (!n) {
        return 0.0;
    }

    tmp = malloc(n * sizeof(*tmp));
    if (!tmp) {
        return 0.0;
    }

    memcpy(tmp, v, n * sizeof(*tmp));
    qsort(tmp, n, sizeof(*tmp), cmp_double);

    if (n & 1) {
        result = tmp[n / 2];
    }
    else {
        result = 0.5 * (tmp[n / 2 - 1] + tmp[n / 2]);
    }

    free(tmp);
    return result;
}

static double fft_bin_freq(size_t bin, size_t nfft, double sample_rate)
{
    if (bin <= nfft / 2) {
        return (double)bin * sample_rate / (double)nfft;
    }

    return ((double)bin - (double)nfft) * sample_rate / (double)nfft;
}

static cpx_t cu8_sample(uint8_t const *iq_buf, size_t sample)
{
    cpx_t value = {
            (double)iq_buf[sample * 2] - 127.5,
            (double)iq_buf[sample * 2 + 1] - 127.5};

    return value;
}

int psk_find_candidate(
        uint8_t const *iq_buf,
        size_t sample_count,
        unsigned sample_size,
        unsigned sample_rate,
        psk_candidate_t *candidate)
{
    size_t window_samples;
    size_t rows;
    size_t nfft = 1;
    size_t row;
    int found = 0;
    psk_candidate_t global_best = {0};

    if (!iq_buf || !candidate || !sample_rate) {
        return 0;
    }

    /* Currently validated for unsigned 8-bit interleaved I/Q only. */
    if (sample_size != 2) {
        return 0;
    }

    window_samples = (size_t)((double)sample_rate * 0.004);
    if (window_samples < 256) {
        window_samples = 256;
    }

    rows = sample_count / window_samples;
    if (!rows) {
        return 0;
    }

    while (nfft < window_samples) {
        nfft <<= 1;
    }

    for (row = 0; row < rows; row++) {
        cpx_t *buf;
        double *bg;
        size_t bg_count = 0;
        size_t k;
        size_t top_bins[16] = {0};
        double top_power[16] = {0};
        cpx_t mean = {0.0, 0.0};
        double bg_hi;
        double background;
        double row_best_score = 0.0;
        double row_best_midpoint = 0.0;
        double row_best_sep = 0.0;

        buf = calloc(nfft, sizeof(*buf));
        if (!buf) {
            return 0;
        }

        bg = malloc(nfft * sizeof(*bg));
        if (!bg) {
            free(buf);
            return 0;
        }

        for (k = 0; k < window_samples; k++) {
            mean = c_add(mean, cu8_sample(iq_buf, row * window_samples + k));
        }
        mean = c_scale(mean, 1.0 / (double)window_samples);

        for (k = 0; k < window_samples; k++) {
            double w = 0.5 - 0.5 * cos(
                                         2.0 * M_PI * (double)k /
                                         (double)(window_samples - 1));
            cpx_t sample = cu8_sample(iq_buf, row * window_samples + k);

            buf[k] = c_scale(c_sub(sample, mean), w);
        }

        fft_inplace(buf, nfft);

        bg_hi = (double)sample_rate / 2.0 * 0.90;
        if (bg_hi > 110000.0) {
            bg_hi = 110000.0;
        }

        for (k = 0; k < nfft; k++) {
            double f = fft_bin_freq(k, nfft, (double)sample_rate);
            double af = fabs(f);
            double p = c_abs2(buf[k]);

            if (af > 2000.0 && af < 60000.0) {
                int slot;

                for (slot = 0; slot < 16; slot++) {
                    if (p > top_power[slot]) {
                        int s;

                        for (s = 15; s > slot; s--) {
                            top_power[s] = top_power[s - 1];
                            top_bins[s] = top_bins[s - 1];
                        }

                        top_power[slot] = p;
                        top_bins[slot] = k;
                        break;
                    }
                }
            }

            if (af > 60000.0 && af < bg_hi) {
                bg[bg_count++] = p;
            }
        }

        if (bg_count < 5) {
            bg_count = 0;

            for (k = 0; k < nfft; k++) {
                double f = fft_bin_freq(k, nfft, (double)sample_rate);
                double af = fabs(f);

                if (af > 40000.0 &&
                        af < (double)sample_rate / 2.0 * 0.90) {
                    bg[bg_count++] = c_abs2(buf[k]);
                }
            }
        }

        if (bg_count >= 5) {
            int a;
            int b;

            background = median_copy(bg, bg_count) + 1e-12;

            for (a = 0; a < 16; a++) {
                if (top_power[a] <= 0.0) {
                    continue;
                }

                for (b = a + 1; b < 16; b++) {
                    double fi;
                    double fj;
                    double sep;
                    double midpoint;
                    double score;

                    if (top_power[b] <= 0.0) {
                        continue;
                    }

                    fi = fft_bin_freq(
                            top_bins[a], nfft, (double)sample_rate);
                    fj = fft_bin_freq(
                            top_bins[b], nfft, (double)sample_rate);

                    if (fj < fi) {
                        double t = fi;
                        fi = fj;
                        fj = t;
                    }

                    sep = fj - fi;
                    midpoint = (fi + fj) / 2.0;

                    if (sep >= 34500.0 &&
                            sep <= 37500.0 &&
                            fabs(midpoint) <= 22000.0) {
                        double weaker =
                                top_power[a] < top_power[b] ?
                                        top_power[a] :
                                        top_power[b];

                        score = weaker / background;

                        if (score > row_best_score) {
                            row_best_score = score;
                            row_best_midpoint = midpoint;
                            row_best_sep = sep;
                        }
                    }
                }
            }

            if (row_best_score > 20.0) {
                psk_candidate_t detected;

                detected.offset_s =
                        (double)(row * window_samples) /
                        (double)sample_rate;
                detected.signature_db =
                        10.0 * log10(row_best_score);
                detected.carrier_offset_hz =
                        row_best_midpoint;
                detected.symbol_rate_hint =
                        row_best_sep;

                if (!found ||
                        detected.signature_db >
                                global_best.signature_db) {
                    global_best = detected;
                    found = 1;
                }
            }
        }

        free(buf);
        free(bg);
    }

    if (found) {
        *candidate = global_best;
    }

    return found;
}

int psk_demod_candidate(
        uint8_t const *iq_buf,
        size_t sample_count,
        unsigned sample_size,
        unsigned sample_rate,
        psk_candidate_t const *candidate,
        bitbuffer_t *bitbuffer)
{
    (void)iq_buf;
    (void)sample_count;
    (void)sample_size;
    (void)sample_rate;
    (void)candidate;
    (void)bitbuffer;

    return 0;
}
