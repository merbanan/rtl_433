/*
 * TX63U-IT standalone PSK demodulation proof-of-concept
 *
 * Port of the working Python decoder used for La Crosse TX63U-IT / FCC OMO-M-12.
 * This file is intentionally isolated from rtl_433 core. It reads CU8 I/Q files,
 * detects the ~36 kHz phase-signature sidebands, differentially demodulates the
 * ~36 ksymbol/s BPSK-like signal, finds HDLC flags, removes bit stuffing, and
 * validates CRC-16/X-25.
 *
 * This is development/test code, not yet an rtl_433 device implementation.
 */

#define _CRT_SECURE_NO_WARNINGS

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    double re;
    double im;
} cpx_t;

typedef struct {
    double offset_s;
    double signature_db;
    double carrier_offset_hz;
    double symbol_rate_hint;
} detection_t;

static const char *directions[16] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};

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

static cpx_t c_conj(cpx_t a)
{
    cpx_t r = {a.re, -a.im};
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

static double c_abs(cpx_t a)
{
    return sqrt(c_abs2(a));
}

static cpx_t c_expj(double phase)
{
    cpx_t r = {cos(phase), sin(phase)};
    return r;
}

static void fft_inplace(cpx_t *a, size_t n)
{
    size_t i, j, len;

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

static int cmp_double(const void *pa, const void *pb)
{
    double a = *(const double *)pa;
    double b = *(const double *)pb;
    return (a > b) - (a < b);
}

static double median_copy(const double *v, size_t n)
{
    double *tmp;
    double result;

    if (!n) {
        return 0.0;
    }

    tmp = (double *)malloc(n * sizeof(*tmp));
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

static uint16_t crc16_x25(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xffff;
    size_t i;
    int bit;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
        }
    }

    return (uint16_t)(crc ^ 0xffff);
}

static int flag_at(const uint8_t *bits, size_t nbits, size_t pos)
{
    static const uint8_t flag_bits[8] = {0, 1, 1, 1, 1, 1, 1, 0};
    size_t k;

    if (pos + 8 > nbits) {
        return 0;
    }
    for (k = 0; k < 8; k++) {
        if (bits[pos + k] != flag_bits[k]) {
            return 0;
        }
    }
    return 1;
}

static int destuff_72(const uint8_t *in, size_t nin, uint8_t out[72])
{
    size_t i = 0;
    size_t o = 0;
    int ones = 0;

    while (i < nin) {
        uint8_t b = in[i++];

        if (o >= 72) {
            return 0;
        }
        out[o++] = b;

        if (b) {
            ones++;
            if (ones == 5) {
                if (i >= nin || in[i] != 0) {
                    return 0;
                }
                i++;
                ones = 0;
            }
        }
        else {
            ones = 0;
        }
    }

    return o == 72;
}

static void bits_to_bytes_lsb(const uint8_t bits[72], uint8_t bytes[9])
{
    size_t i, k;

    memset(bytes, 0, 9);
    for (i = 0; i < 9; i++) {
        for (k = 0; k < 8; k++) {
            bytes[i] |= (uint8_t)((bits[i * 8 + k] & 1) << k);
        }
    }
}

static int find_valid_frame(const uint8_t *bits, size_t nbits, uint8_t frame[9])
{
    size_t start, end;

    for (start = 0; start + 8 <= nbits; start++) {
        uint8_t destuffed[72];
        uint8_t body[9];
        uint16_t received;
        uint16_t calculated;
        size_t separation;

        if (!flag_at(bits, nbits, start)) {
            continue;
        }

        for (end = start + 8; end + 8 <= nbits; end++) {
            if (!flag_at(bits, nbits, end)) {
                continue;
            }

            separation = end - start;
            if (separation > 110) {
                break;
            }
            if (separation < 75) {
                continue;
            }

            if (!destuff_72(bits + start + 8, end - (start + 8), destuffed)) {
                continue;
            }

            bits_to_bytes_lsb(destuffed, body);

            if (body[0] != 0x3c || body[1] != 0xc0) {
                continue;
            }

            received = (uint16_t)(body[7] | ((uint16_t)body[8] << 8));
            calculated = crc16_x25(body, 7);

            if (received == calculated) {
                memcpy(frame, body, 9);
                return 1;
            }
        }
    }

    return 0;
}

static int load_cu8(const char *filename, cpx_t **out_iq, size_t *out_count)
{
    FILE *fp;
    long bytes;
    uint8_t *raw;
    cpx_t *iq;
    size_t samples;
    size_t i;

    fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "ERROR: cannot open %s\n", filename);
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    bytes = ftell(fp);
    if (bytes < 2) {
        fclose(fp);
        fprintf(stderr, "ERROR: input file is empty\n");
        return 0;
    }
    rewind(fp);

    if (bytes & 1) {
        bytes--;
    }

    raw = (uint8_t *)malloc((size_t)bytes);
    if (!raw) {
        fclose(fp);
        return 0;
    }

    if (fread(raw, 1, (size_t)bytes, fp) != (size_t)bytes) {
        free(raw);
        fclose(fp);
        fprintf(stderr, "ERROR: short read from %s\n", filename);
        return 0;
    }
    fclose(fp);

    samples = (size_t)bytes / 2;
    iq = (cpx_t *)malloc(samples * sizeof(*iq));
    if (!iq) {
        free(raw);
        return 0;
    }

    for (i = 0; i < samples; i++) {
        iq[i].re = (double)raw[i * 2] - 127.5;
        iq[i].im = (double)raw[i * 2 + 1] - 127.5;
    }

    free(raw);
    *out_iq = iq;
    *out_count = samples;
    return 1;
}

static double fft_bin_freq(size_t bin, size_t nfft, double sample_rate)
{
    if (bin <= nfft / 2) {
        return (double)bin * sample_rate / (double)nfft;
    }
    return ((double)bin - (double)nfft) * sample_rate / (double)nfft;
}

static int detect_best_candidate(
        const cpx_t *iq,
        size_t count,
        int sample_rate,
        detection_t *best_out)
{
    size_t window_samples = (size_t)(sample_rate * 0.004);
    size_t rows;
    size_t nfft = 1;
    size_t row;
    int found = 0;
    detection_t global_best = {0};

    if (window_samples < 256) {
        window_samples = 256;
    }
    rows = count / window_samples;
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
        cpx_t mean = {0, 0};
        double bg_hi;
        double background;
        double row_best_score = 0.0;
        double row_best_midpoint = 0.0;
        double row_best_sep = 0.0;

        buf = (cpx_t *)calloc(nfft, sizeof(*buf));
        bg = (double *)malloc(nfft * sizeof(*bg));
        if (!buf || !bg) {
            free(buf);
            free(bg);
            return 0;
        }

        for (k = 0; k < window_samples; k++) {
            mean = c_add(mean, iq[row * window_samples + k]);
        }
        mean = c_scale(mean, 1.0 / (double)window_samples);

        for (k = 0; k < window_samples; k++) {
            double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)k / (double)(window_samples - 1));
            buf[k] = c_scale(c_sub(iq[row * window_samples + k], mean), w);
        }

        fft_inplace(buf, nfft);

        bg_hi = sample_rate / 2.0 * 0.90;
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
                if (af > 40000.0 && af < sample_rate / 2.0 * 0.90) {
                    bg[bg_count++] = c_abs2(buf[k]);
                }
            }
        }

        if (bg_count >= 5) {
            int a, b;
            background = median_copy(bg, bg_count) + 1e-12;

            for (a = 0; a < 16; a++) {
                if (top_power[a] <= 0.0) {
                    continue;
                }
                for (b = a + 1; b < 16; b++) {
                    double fi, fj, sep, midpoint, score;
                    if (top_power[b] <= 0.0) {
                        continue;
                    }

                    fi = fft_bin_freq(top_bins[a], nfft, (double)sample_rate);
                    fj = fft_bin_freq(top_bins[b], nfft, (double)sample_rate);
                    if (fj < fi) {
                        double t = fi;
                        fi = fj;
                        fj = t;
                    }

                    sep = fj - fi;
                    midpoint = (fi + fj) / 2.0;
                    if (sep >= 34500.0 && sep <= 37500.0 && fabs(midpoint) <= 22000.0) {
                        double weaker = top_power[a] < top_power[b] ? top_power[a] : top_power[b];
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
                detection_t d;
                d.offset_s = (double)(row * window_samples) / (double)sample_rate;
                d.signature_db = 10.0 * log10(row_best_score);
                d.carrier_offset_hz = row_best_midpoint;
                d.symbol_rate_hint = row_best_sep;

                if (!found || d.signature_db > global_best.signature_db) {
                    global_best = d;
                    found = 1;
                }
            }
        }

        free(buf);
        free(bg);
    }

    if (found) {
        *best_out = global_best;
    }
    return found;
}

static int demod_bits(
        const cpx_t *iq,
        size_t count,
        int sample_rate,
        double symbol_rate,
        double carrier_offset_hz,
        double phase_samples,
        uint8_t **bits_out,
        size_t *nbits_out)
{
    cpx_t *mixed;
    cpx_t *cumulative;
    cpx_t *symbols;
    cpx_t *diff;
    double *magnitudes;
    double samples_per_symbol;
    size_t symbol_count;
    size_t i;
    double threshold;
    cpx_t sq_sum = {0, 0};
    double residual_phase;
    cpx_t correction;
    uint8_t *bits;

    samples_per_symbol = (double)sample_rate / symbol_rate;
    if ((double)count <= phase_samples + samples_per_symbol * 102.0) {
        return 0;
    }

    symbol_count = (size_t)(((double)count - phase_samples) / samples_per_symbol) - 1;
    if (symbol_count < 100) {
        return 0;
    }

    mixed = (cpx_t *)malloc(count * sizeof(*mixed));
    cumulative = (cpx_t *)malloc((count + 1) * sizeof(*cumulative));
    symbols = (cpx_t *)malloc(symbol_count * sizeof(*symbols));
    diff = (cpx_t *)malloc((symbol_count - 1) * sizeof(*diff));
    magnitudes = (double *)malloc((symbol_count - 1) * sizeof(*magnitudes));
    bits = (uint8_t *)malloc((symbol_count - 1) * sizeof(*bits));

    if (!mixed || !cumulative || !symbols || !diff || !magnitudes || !bits) {
        free(mixed);
        free(cumulative);
        free(symbols);
        free(diff);
        free(magnitudes);
        free(bits);
        return 0;
    }

    for (i = 0; i < count; i++) {
        double phase = -2.0 * M_PI * carrier_offset_hz * (double)i / (double)sample_rate;
        mixed[i] = c_mul(iq[i], c_expj(phase));
    }

    cumulative[0].re = 0.0;
    cumulative[0].im = 0.0;
    for (i = 0; i < count; i++) {
        cumulative[i + 1] = c_add(cumulative[i], mixed[i]);
    }

    for (i = 0; i < symbol_count; i++) {
        long start = lround(phase_samples + (double)i * samples_per_symbol);
        long end = lround(phase_samples + (double)(i + 1) * samples_per_symbol);
        int width;

        if (start < 0) {
            start = 0;
        }
        if (end > (long)count) {
            end = (long)count;
        }
        width = (int)(end - start);
        if (width <= 0) {
            free(mixed);
            free(cumulative);
            free(symbols);
            free(diff);
            free(magnitudes);
            free(bits);
            return 0;
        }

        symbols[i] = c_scale(c_sub(cumulative[end], cumulative[start]), 1.0 / (double)width);
    }

    for (i = 0; i + 1 < symbol_count; i++) {
        diff[i] = c_mul(symbols[i + 1], c_conj(symbols[i]));
        magnitudes[i] = c_abs(diff[i]);
    }

    {
        double *sorted = (double *)malloc((symbol_count - 1) * sizeof(*sorted));
        size_t idx;
        if (!sorted) {
            free(mixed);
            free(cumulative);
            free(symbols);
            free(diff);
            free(magnitudes);
            free(bits);
            return 0;
        }
        memcpy(sorted, magnitudes, (symbol_count - 1) * sizeof(*sorted));
        qsort(sorted, symbol_count - 1, sizeof(*sorted), cmp_double);
        idx = (size_t)(0.20 * (double)(symbol_count - 1));
        if (idx >= symbol_count - 1) {
            idx = symbol_count - 2;
        }
        threshold = sorted[idx];
        free(sorted);
    }

    for (i = 0; i + 1 < symbol_count; i++) {
        if (magnitudes[i] > threshold) {
            cpx_t normalized;
            cpx_t squared;
            double denom = magnitudes[i] + 1e-12;

            normalized = c_scale(diff[i], 1.0 / denom);
            squared = c_mul(normalized, normalized);
            sq_sum = c_add(sq_sum, squared);
        }
    }

    residual_phase = 0.5 * atan2(sq_sum.im, sq_sum.re);
    correction = c_expj(-residual_phase);

    for (i = 0; i + 1 < symbol_count; i++) {
        cpx_t corrected = c_mul(diff[i], correction);
        bits[i] = corrected.re >= 0.0 ? 1 : 0;
    }

    free(mixed);
    free(cumulative);
    free(symbols);
    free(diff);
    free(magnitudes);

    *bits_out = bits;
    *nbits_out = symbol_count - 1;
    return 1;
}

static int try_frame_for_bits(const uint8_t *bits, size_t nbits, uint8_t frame[9])
{
    uint8_t *inv;
    size_t i;

    if (find_valid_frame(bits, nbits, frame)) {
        return 1;
    }

    inv = (uint8_t *)malloc(nbits);
    if (!inv) {
        return 0;
    }
    for (i = 0; i < nbits; i++) {
        inv[i] = bits[i] ^ 1;
    }

    i = find_valid_frame(inv, nbits, frame);
    free(inv);
    return (int)i;
}

static int decode_candidate(
        const cpx_t *iq,
        size_t count,
        int sample_rate,
        double carrier_offset_hint,
        double symbol_rate_hint,
        uint8_t frame[9],
        double *used_rate)
{
    int delta;
    int rate_index = 0;
    double rates[41];

    rates[rate_index++] = symbol_rate_hint;
    for (delta = 50; delta <= 1000; delta += 50) {
        rates[rate_index++] = symbol_rate_hint - delta;
        rates[rate_index++] = symbol_rate_hint + delta;
    }

    for (int r = 0; r < rate_index; r++) {
        double symbol_rate = rates[r];
        double samples_per_symbol;

        if (symbol_rate < 35000.0 || symbol_rate > 37000.0) {
            continue;
        }

        samples_per_symbol = (double)sample_rate / symbol_rate;

        for (int p = 0; p < 16; p++) {
            double phase = samples_per_symbol * (double)p / 16.0;
            uint8_t *bits = NULL;
            size_t nbits = 0;

            if (!demod_bits(
                        iq, count, sample_rate, symbol_rate,
                        carrier_offset_hint, phase, &bits, &nbits)) {
                continue;
            }

            if (try_frame_for_bits(bits, nbits, frame)) {
                free(bits);
                *used_rate = symbol_rate;
                return 1;
            }

            free(bits);
        }
    }

    return 0;
}

static void print_frame(const uint8_t frame[9], const detection_t *d, double used_rate)
{
    int direction_code = (frame[2] >> 4) & 0x0f;
    int speed_raw = ((frame[2] & 0x0f) << 8) | frame[3];
    int gust_direction_code = (frame[4] >> 4) & 0x0f;
    int gust_raw = ((frame[4] & 0x0f) << 8) | frame[5];
    int i;

    printf("frame=");
    for (i = 0; i < 9; i++) {
        printf("%02X%s", frame[i], i == 8 ? "" : " ");
    }
    printf("\n");

    printf("wind=%.1f m/s %s  gust=%.1f m/s %s\n",
            speed_raw / 10.0,
            directions[direction_code],
            gust_raw / 10.0,
            directions[gust_direction_code]);

    printf("status=0x%02X  CRC=OK  signature=%.1f dB  carrier_offset=%.1f Hz  symbol_rate=%.1f\n",
            frame[6],
            d->signature_db,
            d->carrier_offset_hz,
            used_rate);
}

static int infer_sample_rate(const char *filename)
{
    const char *p;

    p = strstr(filename, "_1000k");
    if (p) {
        return 1000000;
    }

    p = strstr(filename, "_250k");
    if (p) {
        return 250000;
    }

    return 250000;
}

int main(int argc, char **argv)
{
    const char *filename;
    int sample_rate;
    cpx_t *iq = NULL;
    size_t count = 0;
    detection_t d;
    size_t center;
    size_t half;
    size_t start;
    size_t end;
    uint8_t frame[9];
    double used_rate = 0.0;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <capture.cu8> [sample_rate]\n", argv[0]);
        return 2;
    }

    filename = argv[1];
    sample_rate = argc == 3 ? atoi(argv[2]) : infer_sample_rate(filename);
    if (sample_rate <= 0) {
        fprintf(stderr, "ERROR: invalid sample rate\n");
        return 2;
    }

    printf("Decoding %s at %d samples/s\n", filename, sample_rate);

    if (!load_cu8(filename, &iq, &count)) {
        return 2;
    }

    if (!detect_best_candidate(iq, count, sample_rate, &d)) {
        fprintf(stderr, "No TX63 phase-signature candidate found.\n");
        free(iq);
        return 1;
    }

    printf("candidate t=%.6f s  signature=%.1f dB  carrier_offset=%.1f Hz  symbol_hint=%.1f\n",
            d.offset_s, d.signature_db, d.carrier_offset_hz, d.symbol_rate_hint);

    center = (size_t)(d.offset_s * sample_rate);
    half = (size_t)(0.012 * sample_rate);
    start = center > half ? center - half : 0;
    end = center + half < count ? center + half : count;

    if (end <= start || end - start < (size_t)(0.015 * sample_rate)) {
        fprintf(stderr, "Candidate window too short.\n");
        free(iq);
        return 1;
    }

    if (!decode_candidate(
                iq + start,
                end - start,
                sample_rate,
                d.carrier_offset_hz,
                d.symbol_rate_hint,
                frame,
                &used_rate)) {
        fprintf(stderr, "Candidate found, but no CRC-valid TX63 frame decoded.\n");
        free(iq);
        return 1;
    }

    print_frame(frame, &d, used_rate);
    free(iq);
    return 0;
}
