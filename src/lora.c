/** @file
    Fixed-point LoRa PHY demodulator.

    The receiver follows the conventional dechirp-and-FFT design: repeated
    upchirps acquire the preamble, downchirps align the SFD, and each data
    chirp is reduced to an FFT-bin index. All IQ-domain operations use integer
    arithmetic and Q15 coefficients.

    The PHY decoding flow is based on LoRaPHY and the accompanying paper:
    https://github.com/jkadbear/LoRaPHY
    https://dl.acm.org/doi/10.1145/3546869
*/

#include "lora.h"
#include "bit_util.h"
#include "fft.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define LORA_MIN_SF 7U
#define LORA_MAX_SF 12U
#define LORA_SINE_STEPS 1024U
#define LORA_PREAMBLE_MATCHES 5U
#define LORA_ANALYSIS_SYMBOLS LORA_PREAMBLE_MATCHES
#define LORA_KEEP_SYMBOLS 20U
#define LORA_MAX_SYMBOLS 640U
#define LORA_MAX_OVERSAMPLING 16U
#define LORA_REFINE_PADDING 4
#define LORA_REFINED_BIN_SCALE 4096
#define LORA_MAX_FFT_ORDER 18U
#define LORA_REPAIR_MAX_CHANGES 8U
#define LORA_AUTO_BANDWIDTH_COUNT 4U
#define LORA_AUTO_STATE_COUNT \
    ((LORA_MAX_SF - LORA_MIN_SF + 1) * LORA_AUTO_BANDWIDTH_COUNT)

typedef struct {
    int16_t re;
    int16_t im;
} lora_sample_t;

typedef struct {
    fft_fixed_twiddle_t *twiddle;
    uint32_t *bit_reverse;
    unsigned length;
} lora_fft_plan_t;

typedef struct {
    lora_fft_plan_t plans[LORA_MAX_FFT_ORDER + 1];
} lora_fft_cache_t;

struct lora_fft_demod {
    lora_sample_t *samples;
    size_t sample_count;
    size_t capacity;
    uint64_t sample_offset;
    fft_fixed_sample_t *fft;
    lora_sample_t *dechirp;
    lora_fft_plan_t *fft_plan;
    lora_fft_cache_t *fft_cache;
    int owns_fft_cache;
    unsigned workspace_capacity;
    fft_fixed_sample_t *refine_fft;
    lora_fft_plan_t *refine_plan;
    unsigned refine_fft_len;
    unsigned spreading_factor;
    unsigned chips;
    unsigned fft_len;
    unsigned phase_steps;
    unsigned oversampling;
    uint32_t sample_rate;
    uint32_t bandwidth;
    unsigned sample_shift;
    unsigned detected_spreading_factor;
    uint32_t detected_bandwidth;
    unsigned detected_state;
    int candidate_incomplete;
    struct lora_fft_demod *auto_states[LORA_AUTO_STATE_COUNT];
};

static uint32_t const auto_bandwidths[LORA_AUTO_BANDWIDTH_COUNT] = {
        500000, 250000, 125000, 62500,
};

/* LoRa PHY whitening LFSR x^8+x^6+x^5+x^4+1, seeded with 0xff. */
static uint8_t const whitening_bytes[LORA_MAX_PAYLOAD_LEN] = {
        0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe1, 0xc2, 0x85, 0x0b, 0x17, 0x2f, 0x5e, 0xbc, 0x78, 0xf1, 0xe3,
        0xc6, 0x8d, 0x1a, 0x34, 0x68, 0xd0, 0xa0, 0x40, 0x80, 0x01, 0x02, 0x04, 0x08, 0x11, 0x23, 0x47,
        0x8e, 0x1c, 0x38, 0x71, 0xe2, 0xc4, 0x89, 0x12, 0x25, 0x4b, 0x97, 0x2e, 0x5c, 0xb8, 0x70, 0xe0,
        0xc0, 0x81, 0x03, 0x06, 0x0c, 0x19, 0x32, 0x64, 0xc9, 0x92, 0x24, 0x49, 0x93, 0x26, 0x4d, 0x9b,
        0x37, 0x6e, 0xdc, 0xb9, 0x72, 0xe4, 0xc8, 0x90, 0x20, 0x41, 0x82, 0x05, 0x0a, 0x15, 0x2b, 0x56,
        0xad, 0x5b, 0xb6, 0x6d, 0xda, 0xb5, 0x6b, 0xd6, 0xac, 0x59, 0xb2, 0x65, 0xcb, 0x96, 0x2c, 0x58,
        0xb0, 0x61, 0xc3, 0x87, 0x0f, 0x1f, 0x3e, 0x7d, 0xfb, 0xf6, 0xed, 0xdb, 0xb7, 0x6f, 0xde, 0xbd,
        0x7a, 0xf5, 0xeb, 0xd7, 0xae, 0x5d, 0xba, 0x74, 0xe8, 0xd1, 0xa2, 0x44, 0x88, 0x10, 0x21, 0x43,
        0x86, 0x0d, 0x1b, 0x36, 0x6c, 0xd8, 0xb1, 0x63, 0xc7, 0x8f, 0x1e, 0x3c, 0x79, 0xf3, 0xe7, 0xce,
        0x9c, 0x39, 0x73, 0xe6, 0xcc, 0x98, 0x31, 0x62, 0xc5, 0x8b, 0x16, 0x2d, 0x5a, 0xb4, 0x69, 0xd2,
        0xa4, 0x48, 0x91, 0x22, 0x45, 0x8a, 0x14, 0x29, 0x52, 0xa5, 0x4a, 0x95, 0x2a, 0x54, 0xa9, 0x53,
        0xa7, 0x4e, 0x9d, 0x3b, 0x77, 0xee, 0xdd, 0xbb, 0x76, 0xec, 0xd9, 0xb3, 0x67, 0xcf, 0x9e, 0x3d,
        0x7b, 0xf7, 0xef, 0xdf, 0xbf, 0x7e, 0xfd, 0xfa, 0xf4, 0xe9, 0xd3, 0xa6, 0x4c, 0x99, 0x33, 0x66,
        0xcd, 0x9a, 0x35, 0x6a, 0xd4, 0xa8, 0x51, 0xa3, 0x46, 0x8c, 0x18, 0x30, 0x60, 0xc1, 0x83, 0x07,
        0x0e, 0x1d, 0x3a, 0x75, 0xea, 0xd5, 0xaa, 0x55, 0xab, 0x57, 0xaf, 0x5f, 0xbe, 0x7c, 0xf9, 0xf2,
        0xe5, 0xca, 0x94, 0x28, 0x50, 0xa1, 0x42, 0x84, 0x09, 0x13, 0x27, 0x4f, 0x9f, 0x3f, 0x7f,
};

static int16_t const sine_quarter_q15[257] = {
        0, 201, 402, 603, 804, 1005, 1206, 1407, 1608, 1809, 2009, 2210, 2410, 2611, 2811, 3012,
        3212, 3412, 3612, 3811, 4011, 4210, 4410, 4609, 4808, 5007, 5205, 5404, 5602, 5800, 5998, 6195,
        6393, 6590, 6786, 6983, 7179, 7375, 7571, 7767, 7962, 8157, 8351, 8545, 8739, 8933, 9126, 9319,
        9512, 9704, 9896, 10087, 10278, 10469, 10659, 10849, 11039, 11228, 11417, 11605, 11793, 11980, 12167, 12353,
        12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828, 14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269,
        15446, 15623, 15800, 15976, 16151, 16325, 16499, 16673, 16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
        18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357, 19519, 19680, 19841, 20000, 20159, 20317, 20475, 20631,
        20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856, 22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027,
        23170, 23311, 23452, 23592, 23731, 23870, 24007, 24143, 24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
        25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198, 26319, 26438, 26556, 26674, 26790, 26905, 27019, 27133,
        27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001, 28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803,
        28898, 28992, 29085, 29177, 29268, 29358, 29447, 29534, 29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
        30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783, 30852, 30919, 30985, 31050, 31113, 31176, 31237, 31297,
        31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736, 31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098,
        32137, 32176, 32213, 32250, 32285, 32318, 32351, 32382, 32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
        32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717, 32728, 32737, 32745, 32752, 32757, 32761, 32765, 32766,
        32767,
};

static int16_t sin_table_q15(unsigned phase)
{
    unsigned const p = phase & (LORA_SINE_STEPS - 1);
    unsigned const quadrant = p >> 8;
    unsigned const index = p & 0xff;

    switch (quadrant) {
        case 0: return sine_quarter_q15[index];
        case 1: return sine_quarter_q15[256 - index];
        case 2: return (int16_t)-sine_quarter_q15[index];
        default: return (int16_t)-sine_quarter_q15[256 - index];
    }
}

static int16_t sin_q15(uint32_t phase, unsigned phase_steps)
{
    unsigned const p = phase & (phase_steps - 1);
    if (phase_steps <= LORA_SINE_STEPS) {
        return sin_table_q15(p * (LORA_SINE_STEPS / phase_steps));
    }
    unsigned const interpolation_steps = phase_steps / LORA_SINE_STEPS;
    unsigned const index = p / interpolation_steps;
    unsigned const fraction = p & (interpolation_steps - 1);
    int32_t const a = sin_table_q15(index);
    int32_t const b = sin_table_q15(index + 1);
    return (int16_t)(a + ((b - a) * (int32_t)fraction
                            + (int32_t)interpolation_steps / 2)
                    / (int32_t)interpolation_steps);
}

static int16_t cos_q15(uint32_t phase, unsigned phase_steps)
{
    return sin_q15(phase + phase_steps / 4, phase_steps);
}

static lora_fft_plan_t *get_fft_plan(lora_fft_cache_t *cache, unsigned length)
{
    unsigned const order = fft_fixed_order(length);
    if (!cache || !order || order > LORA_MAX_FFT_ORDER) {
        return NULL;
    }
    lora_fft_plan_t *plan = &cache->plans[order];
    if (plan->length == length) {
        return plan;
    }
    if (plan->length) {
        return NULL;
    }

    fft_fixed_twiddle_t *twiddle = malloc(length * sizeof(*twiddle));
    if (!twiddle) {
        return NULL;
    }
    uint32_t *bit_reverse = malloc(length * sizeof(*bit_reverse));
    if (!bit_reverse) {
        free(twiddle);
        return NULL;
    }
    for (unsigned n = 0; n < length; ++n) {
        bit_reverse[n] = fft_fixed_mixed_reverse(n, order);
        twiddle[n].re = cos_q15(0U - n, length);
        twiddle[n].im = sin_q15(0U - n, length);
    }
    plan->twiddle = twiddle;
    plan->bit_reverse = bit_reverse;
    plan->length = length;
    return plan;
}

static int configure_phy(lora_fft_demod_t *demod, unsigned spreading_factor,
        uint32_t sample_rate, uint32_t bandwidth)
{
    if (spreading_factor < LORA_MIN_SF || spreading_factor > LORA_MAX_SF
            || !bandwidth || sample_rate % bandwidth) {
        return 0;
    }
    unsigned const oversampling = sample_rate / bandwidth;
    if (oversampling < 2 || oversampling > LORA_MAX_OVERSAMPLING
            || (oversampling & (oversampling - 1))) {
        return 0;
    }
    if (demod->spreading_factor == spreading_factor
            && demod->sample_rate == sample_rate && demod->bandwidth == bandwidth) {
        return 1;
    }
    if (demod->spreading_factor && (demod->spreading_factor != spreading_factor
                || demod->sample_rate != sample_rate || demod->bandwidth != bandwidth)) {
        lora_fft_demod_reset(demod);
    }
    if (demod->spreading_factor == spreading_factor
            && demod->oversampling == oversampling) {
        demod->sample_rate = sample_rate;
        demod->bandwidth = bandwidth;
        return 1;
    }

    unsigned const chips = 1U << spreading_factor;
    unsigned const fft_len = oversampling * chips;
    if (demod->workspace_capacity < fft_len) {
        fft_fixed_sample_t *fft = realloc(demod->fft, fft_len * sizeof(*fft));
        if (!fft) {
            return 0;
        }
        demod->fft = fft;
        lora_sample_t *dechirp = realloc(demod->dechirp, fft_len * sizeof(*dechirp));
        if (!dechirp) {
            return 0;
        }
        demod->dechirp = dechirp;
        demod->workspace_capacity = fft_len;
    }

    lora_fft_plan_t *fft_plan = get_fft_plan(demod->fft_cache, chips);
    if (!fft_plan) {
        return 0;
    }

    demod->spreading_factor = spreading_factor;
    demod->chips = chips;
    demod->fft_len = fft_len;
    demod->phase_steps = 2 * oversampling * fft_len;
    demod->oversampling = oversampling;
    demod->sample_rate = sample_rate;
    demod->bandwidth = bandwidth;
    demod->fft_plan = fft_plan;

    for (unsigned n = 0; n < fft_len; ++n) {
        uint32_t const phase = oversampling * chips * n - n * n;
        demod->dechirp[n].re = cos_q15(phase, demod->phase_steps);
        demod->dechirp[n].im = sin_q15(phase, demod->phase_steps);
    }
    return 1;
}

static unsigned fft_energy_shift(unsigned length, unsigned sample_shift)
{
    /* CU8 has 8 useful bits and CS16 is reduced to 12 bits. After the
       dechirp and unscaled FFT, this bounds each component to 8192, so 16
       folded complex-bin energies still fit in uint32_t. */
    unsigned const order = fft_fixed_order(length);
    return order + sample_shift > 5
            ? order + sample_shift - 5 : 0;
}

static int dechirp_peak(lora_fft_demod_t *demod, size_t offset,
        int is_upchirp, uint32_t *magnitude)
{
    if (offset > demod->sample_count
            || demod->sample_count - offset < demod->fft_len) {
        return -1;
    }

    for (unsigned fold = 0; fold < demod->oversampling; ++fold) {
        unsigned const base = fold * demod->chips;
        for (unsigned position = 0; position < demod->chips; ++position) {
            unsigned const n = demod->oversampling
                    * demod->fft_plan->bit_reverse[position] + fold;
            int32_t const cr = demod->dechirp[n].re;
            int32_t const ci = is_upchirp ? demod->dechirp[n].im : -demod->dechirp[n].im;
            int32_t const re = demod->samples[offset + n].re;
            int32_t const im = demod->samples[offset + n].im;
            demod->fft[base + position].re = (re * cr - im * ci)
                    >> (8 + demod->sample_shift);
            demod->fft[base + position].im = (re * ci + im * cr)
                    >> (8 + demod->sample_shift);
        }
        fft_fixed_transform_scaled(&demod->fft[base], demod->chips,
                demod->fft_plan->twiddle);
    }

    unsigned const energy_shift = demod->oversampling > 4;
    uint32_t best = 0;
    unsigned best_bin = 0;
    for (unsigned bin = 0; bin < demod->chips; ++bin) {
        uint32_t energy = 0;
        for (unsigned fold = 0; fold < demod->oversampling; ++fold) {
            int32_t const re = demod->fft[bin + fold * demod->chips].re
                    >> energy_shift;
            int32_t const im = demod->fft[bin + fold * demod->chips].im
                    >> energy_shift;
            energy += (uint32_t)(re * re + im * im);
        }
        if (energy > best) {
            best = energy;
            best_bin = bin;
        }
    }

    if (magnitude) {
        *magnitude = best;
    }
    return (int)best_bin;
}

static int prepare_refined_fft(lora_fft_demod_t *demod)
{
    unsigned const refined_len = LORA_REFINE_PADDING * demod->fft_len;
    if (demod->refine_fft_len == refined_len) {
        return 1;
    }

    fft_fixed_sample_t *fft = realloc(demod->refine_fft, refined_len * sizeof(*fft));
    if (!fft) {
        return 0;
    }
    demod->refine_fft = fft;
    lora_fft_plan_t *plan = get_fft_plan(demod->fft_cache, refined_len);
    if (!plan) {
        return 0;
    }
    demod->refine_plan = plan;
    demod->refine_fft_len = refined_len;
    return 1;
}

static uint32_t refined_folded_energy(lora_fft_demod_t const *demod,
        unsigned bin)
{
    uint32_t energy = 0;
    unsigned const bins = LORA_REFINE_PADDING * demod->chips;
    unsigned const energy_shift = fft_energy_shift(demod->fft_len,
            demod->sample_shift);
    for (unsigned fold = 0; fold < demod->oversampling; ++fold) {
        fft_fixed_sample_t const value = demod->refine_fft[bin + fold * bins];
        int32_t const re = value.re >> energy_shift;
        int32_t const im = value.im >> energy_shift;
        energy += (uint32_t)(re * re + im * im);
    }
    return energy;
}

/** Return the peak bin in fixed-point chip units after zero padding. */
static int32_t dechirp_peak_refined(lora_fft_demod_t *demod, size_t offset)
{
    if (offset > demod->sample_count
            || demod->sample_count - offset < demod->fft_len
            || !prepare_refined_fft(demod)) {
        return -1;
    }

    for (unsigned position = 0; position < demod->refine_fft_len; ++position) {
        unsigned const n = demod->refine_plan->bit_reverse[position];
        if (n >= demod->fft_len) {
            demod->refine_fft[position].re = 0;
            demod->refine_fft[position].im = 0;
            continue;
        }
        int32_t const cr = demod->dechirp[n].re;
        int32_t const ci = demod->dechirp[n].im;
        int32_t const re = demod->samples[offset + n].re;
        int32_t const im = demod->samples[offset + n].im;
        demod->refine_fft[position].re = (re * cr - im * ci) >> 15;
        demod->refine_fft[position].im = (re * ci + im * cr) >> 15;
    }
    fft_fixed_transform(demod->refine_fft, demod->refine_fft_len,
            demod->refine_plan->twiddle);

    unsigned const bins = LORA_REFINE_PADDING * demod->chips;
    uint32_t best = 0;
    unsigned best_bin = 0;
    for (unsigned bin = 0; bin < bins; ++bin) {
        uint32_t const energy = refined_folded_energy(demod, bin);
        if (energy > best) {
            best = energy;
            best_bin = bin;
        }
    }

    uint32_t left = refined_folded_energy(demod, (best_bin - 1) & (bins - 1));
    uint32_t center = best;
    uint32_t right = refined_folded_energy(demod, (best_bin + 1) & (bins - 1));
    while (left > INT32_MAX / (4 * LORA_REFINED_BIN_SCALE)
            || center > INT32_MAX / (4 * LORA_REFINED_BIN_SCALE)
            || right > INT32_MAX / (4 * LORA_REFINED_BIN_SCALE)) {
        left >>= 1;
        center >>= 1;
        right >>= 1;
    }
    int32_t const curvature = (int32_t)left - 2 * (int32_t)center
            + (int32_t)right;
    int32_t correction = 0;
    if (curvature < 0) {
        correction = ((int32_t)left - (int32_t)right)
                * LORA_REFINED_BIN_SCALE
                / (2 * LORA_REFINE_PADDING * curvature);
        if (correction < -LORA_REFINED_BIN_SCALE
                        / (2 * LORA_REFINE_PADDING)) {
            correction = -LORA_REFINED_BIN_SCALE
                    / (2 * LORA_REFINE_PADDING);
        }
        else if (correction > LORA_REFINED_BIN_SCALE
                        / (2 * LORA_REFINE_PADDING)) {
            correction = LORA_REFINED_BIN_SCALE
                    / (2 * LORA_REFINE_PADDING);
        }
    }
    int32_t result = (int32_t)best_bin
            * (LORA_REFINED_BIN_SCALE / LORA_REFINE_PADDING) + correction;
    int32_t const period = (int32_t)demod->chips * LORA_REFINED_BIN_SCALE;
    if (result < 0) {
        result += period;
    }
    else if (result >= period) {
        result -= period;
    }
    return (int32_t)result;
}

static uint32_t analyzer_dechirp_quality(lora_fft_demod_t const *samples,
        lora_fft_demod_t *candidate, size_t offset, int *peak_bin)
{
    if (offset > samples->sample_count
            || samples->sample_count - offset < candidate->fft_len) {
        return 0;
    }

    for (unsigned fold = 0; fold < candidate->oversampling; ++fold) {
        unsigned const base = fold * candidate->chips;
        for (unsigned position = 0; position < candidate->chips; ++position) {
            unsigned const n = candidate->oversampling
                    * candidate->fft_plan->bit_reverse[position] + fold;
            int32_t const cr = candidate->dechirp[n].re;
            int32_t const ci = candidate->dechirp[n].im;
            int32_t const re = samples->samples[offset + n].re;
            int32_t const im = samples->samples[offset + n].im;
            candidate->fft[base + position].re = (re * cr - im * ci)
                    >> (8 + candidate->sample_shift);
            candidate->fft[base + position].im = (re * ci + im * cr)
                    >> (8 + candidate->sample_shift);
        }
        fft_fixed_transform_scaled(&candidate->fft[base], candidate->chips,
                candidate->fft_plan->twiddle);
    }

    unsigned const energy_shift = candidate->oversampling > 4;
    uint32_t peak = 0;
    uint32_t total = 0;
    unsigned best_bin = 0;
    for (unsigned bin = 0; bin < candidate->chips; ++bin) {
        uint32_t energy = 0;
        for (unsigned fold = 0; fold < candidate->oversampling; ++fold) {
            int32_t const re = candidate->fft[bin + fold * candidate->chips].re
                    >> energy_shift;
            int32_t const im = candidate->fft[bin + fold * candidate->chips].im
                    >> energy_shift;
            energy += (uint32_t)(re * re + im * im);
        }
        total += energy;
        if (energy > peak) {
            peak = energy;
            best_bin = bin;
        }
    }
    if (!total) {
        return 0;
    }
    while (peak > UINT32_MAX / 65535) {
        peak >>= 1;
        total >>= 1;
    }
    if (peak_bin) {
        *peak_bin = (int)best_bin;
    }
    return (uint32_t)(peak * 65535 / total);
}

static unsigned circular_bin_distance(unsigned a, unsigned b, unsigned chips)
{
    unsigned const forward = (a - b) & (chips - 1);
    unsigned const reverse = (b - a) & (chips - 1);
    return forward < reverse ? forward : reverse;
}

static uint8_t hamming_decode(uint8_t word, unsigned redundancy)
{
    if (redundancy >= 7) {
        unsigned const b1 = (word >> 0) & 1;
        unsigned const b2 = (word >> 1) & 1;
        unsigned const b3 = (word >> 2) & 1;
        unsigned const b4 = (word >> 3) & 1;
        unsigned const b5 = (word >> 4) & 1;
        unsigned const b6 = (word >> 5) & 1;
        unsigned const b7 = (word >> 6) & 1;
        unsigned const p2 = b7 ^ b4 ^ b2 ^ b1;
        unsigned const p3 = b5 ^ b3 ^ b2 ^ b1;
        unsigned const p5 = b6 ^ b4 ^ b3 ^ b2;
        unsigned const syndrome = p2 * 4 + p3 * 2 + p5;
        static uint8_t const corrections[8] = {0, 0, 0, 4, 0, 8, 1, 2};
        word ^= corrections[syndrome];
    }
    return word & 0x0f;
}

static void deinterleave(uint16_t const *symbols, unsigned symbol_count,
        unsigned ppm, uint8_t *codewords)
{
    for (unsigned out = 0; out < ppm; ++out) {
        unsigned const rotated_bit = ppm - 1 - out;
        uint8_t word = 0;
        for (unsigned row = 0; row < symbol_count; ++row) {
            unsigned const source_bit = (rotated_bit + row) % ppm;
            unsigned const value = symbols[row] >> (ppm - 1 - source_bit) & 1;
            word |= value << row;
        }
        codewords[out] = word;
    }
}

static void gray_code_symbols(uint16_t const *symbols, unsigned count,
        unsigned spreading_factor, unsigned low_data_rate, uint16_t *gray)
{
    unsigned const mask = (1U << spreading_factor) - 1;
    int bin_offset = 0;
    unsigned previous = 1;
    for (unsigned i = 0; i < count; ++i) {
        unsigned value = symbols[i];
        if (low_data_rate) {
            unsigned const delta = (value - previous) & 3;
            bin_offset -= delta < 2 ? (int)delta : (int)delta - 4;
            previous = value;
            value = (value + bin_offset) & mask;
        }
        value = i < 8 || low_data_rate ? value / 4 : (value - 1) & mask;
        gray[i] = (uint16_t)(value ^ (value >> 1));
    }
}

static int header_checksum_valid(uint8_t const nibbles[5])
{
    static uint8_t const matrix[5][12] = {
            {1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 1},
            {0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0},
            {0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1},
            {0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 1},
    };
    uint8_t bits[12];
    for (unsigned nibble = 0; nibble < 3; ++nibble) {
        for (unsigned bit = 0; bit < 4; ++bit) {
            bits[nibble * 4 + bit] = nibbles[nibble] >> (3 - bit) & 1;
        }
    }

    uint8_t provided[5] = {
            nibbles[3] & 1,
            nibbles[4] >> 3 & 1,
            nibbles[4] >> 2 & 1,
            nibbles[4] >> 1 & 1,
            nibbles[4] & 1,
    };
    for (unsigned row = 0; row < 5; ++row) {
        unsigned parity = 0;
        for (unsigned bit = 0; bit < 12; ++bit) {
            parity ^= matrix[row][bit] & bits[bit];
        }
        if (parity != provided[row]) {
            return 0;
        }
    }
    return 1;
}

static unsigned payload_symbol_count(unsigned payload_len,
        unsigned spreading_factor, unsigned coding_rate, unsigned has_crc,
        unsigned low_data_rate)
{
    int const numerator = 2 * (int)payload_len - (int)spreading_factor
            + 7 + 4 * (int)has_crc;
    unsigned const denominator = spreading_factor - 2 * low_data_rate;
    unsigned const blocks = numerator > 0
            ? ((unsigned)numerator + denominator - 1) / denominator : 0;
    return 8 + (4 + coding_rate) * blocks;
}

static lora_symbols_result_t lora_decode_symbols_mode(uint16_t const *symbols,
        unsigned symbol_count, unsigned spreading_factor, uint32_t bandwidth,
        unsigned low_data_rate, lora_packet_t *packet,
        unsigned *expected_symbol_count)
{
    if (!symbols || !packet || spreading_factor < LORA_MIN_SF
            || spreading_factor > LORA_MAX_SF || !bandwidth) {
        return LORA_SYMBOLS_INVALID;
    }
    if (symbol_count < 8) {
        return LORA_SYMBOLS_INCOMPLETE;
    }

    uint16_t gray[LORA_MAX_SYMBOLS];
    if (symbol_count > LORA_MAX_SYMBOLS) {
        return LORA_SYMBOLS_INVALID;
    }
    gray_code_symbols(symbols, symbol_count, spreading_factor, low_data_rate, gray);

    uint8_t codewords[LORA_MAX_SF];
    uint8_t header[5];
    deinterleave(gray, 8, spreading_factor - 2, codewords);
    for (unsigned i = 0; i < 5; ++i) {
        header[i] = hamming_decode(codewords[i], 8);
    }
    if (!header_checksum_valid(header)) {
        return LORA_SYMBOLS_INVALID;
    }

    unsigned const payload_len = header[0] * 16U + header[1];
    unsigned const has_crc = header[2] & 1;
    unsigned const coding_rate = header[2] >> 1;
    if (!payload_len || payload_len > LORA_MAX_PAYLOAD_LEN
            || coding_rate < 1 || coding_rate > 4 || !has_crc) {
        return LORA_SYMBOLS_INVALID;
    }

    unsigned const expected_symbols = payload_symbol_count(payload_len,
            spreading_factor, coding_rate, has_crc, low_data_rate);
    if (expected_symbol_count) {
        *expected_symbol_count = expected_symbols;
    }
    if (symbol_count < expected_symbols) {
        return LORA_SYMBOLS_INCOMPLETE;
    }

    uint8_t nibbles[2 * (LORA_MAX_PAYLOAD_LEN + 2) + 8];
    unsigned nibble_count = 0;
    for (unsigned i = 5; i < spreading_factor - 2; ++i) {
        nibbles[nibble_count++] = hamming_decode(codewords[i], 8);
    }
    unsigned const redundancy = coding_rate + 4;
    unsigned const payload_ppm = spreading_factor - 2 * low_data_rate;
    for (unsigned offset = 8; offset + redundancy <= expected_symbols; offset += redundancy) {
        deinterleave(&gray[offset], redundancy, payload_ppm, codewords);
        for (unsigned i = 0; i < payload_ppm; ++i) {
            nibbles[nibble_count++] = hamming_decode(codewords[i], redundancy);
        }
    }

    uint8_t bytes[LORA_MAX_PAYLOAD_LEN + 3];
    unsigned const byte_count = nibble_count / 2;
    if (byte_count < payload_len + 2) {
        return LORA_SYMBOLS_INVALID;
    }
    for (unsigned i = 0; i < byte_count && i < sizeof(bytes); ++i) {
        bytes[i] = nibbles[2 * i] | nibbles[2 * i + 1] << 4;
    }

    for (unsigned i = 0; i < payload_len; ++i) {
        packet->payload[i] = bytes[i] ^ whitening_bytes[i];
    }

    uint16_t crc;
    if (payload_len == 1) {
        crc = packet->payload[0];
    }
    else if (payload_len == 2) {
        crc = (uint16_t)packet->payload[1] | (uint16_t)packet->payload[0] << 8;
    }
    else {
        /* LoRa uses the unreflected 0x1021 CRC with an all-zero initial value,
           which is rtl_433's standard crc16() configuration below. Its two
           transmitted CRC bytes are folded into the final payload bytes by
           the PHY whitening/interleaving process, hence the reconstruction
           instead of a direct received-word comparison. */
        uint16_t const base = crc16(packet->payload, payload_len - 2,
                0x1021, 0x0000);
        uint8_t const low = (uint8_t)base ^ packet->payload[payload_len - 1];
        uint8_t const high = (uint8_t)(base >> 8) ^ packet->payload[payload_len - 2];
        crc = (uint16_t)low | (uint16_t)high << 8;
    }
    if (bytes[payload_len] != (uint8_t)crc || bytes[payload_len + 1] != (uint8_t)(crc >> 8)) {
        return LORA_SYMBOLS_INVALID;
    }

    packet->payload_len = payload_len;
    packet->spreading_factor = spreading_factor;
    packet->coding_rate = coding_rate;
    return LORA_SYMBOLS_VALID;
}

lora_symbols_result_t lora_decode_symbols_ex(uint16_t const *symbols,
        unsigned symbol_count, unsigned spreading_factor, uint32_t bandwidth,
        lora_packet_t *packet, unsigned *expected_symbol_count)
{
    unsigned const preferred_mode = spreading_factor >= LORA_MIN_SF
            && spreading_factor <= LORA_MAX_SF
            && bandwidth <= (((1U << spreading_factor) * 1000 - 1) / 16);
    unsigned incomplete_symbols = 0;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        lora_packet_t decoded = {0};
        unsigned expected = 0;
        lora_symbols_result_t const result = lora_decode_symbols_mode(symbols,
                symbol_count, spreading_factor, bandwidth,
                preferred_mode ^ attempt, &decoded, &expected);
        if (result == LORA_SYMBOLS_VALID) {
            *packet = decoded;
            if (expected_symbol_count) {
                *expected_symbol_count = expected;
            }
            return result;
        }
        if (result == LORA_SYMBOLS_INCOMPLETE
                && (!incomplete_symbols || expected < incomplete_symbols)) {
            incomplete_symbols = expected;
        }
    }
    if (incomplete_symbols) {
        if (expected_symbol_count) {
            *expected_symbol_count = incomplete_symbols;
        }
        return LORA_SYMBOLS_INCOMPLETE;
    }
    return symbol_count < 8 ? LORA_SYMBOLS_INCOMPLETE : LORA_SYMBOLS_INVALID;
}

int lora_decode_symbols(uint16_t const *symbols, unsigned symbol_count,
        unsigned spreading_factor, uint32_t bandwidth, lora_packet_t *packet)
{
    return lora_decode_symbols_ex(symbols, symbol_count, spreading_factor,
            bandwidth, packet, NULL) == LORA_SYMBOLS_VALID;
}

typedef struct {
    uint16_t index;
    int8_t delta;
} lora_symbol_alternative_t;

/** Count detectable parity failures in the payload FEC blocks. */
static unsigned payload_fec_errors(uint16_t const *symbols,
        unsigned symbol_count, unsigned spreading_factor,
        unsigned low_data_rate)
{
    uint16_t gray[LORA_MAX_SYMBOLS];
    uint8_t codewords[LORA_MAX_SF];
    uint8_t header[5];
    gray_code_symbols(symbols, symbol_count, spreading_factor,
            low_data_rate, gray);
    deinterleave(gray, 8, spreading_factor - 2, codewords);
    for (unsigned i = 0; i < 5; ++i) {
        header[i] = hamming_decode(codewords[i], 8);
    }
    if (!header_checksum_valid(header)) {
        return UINT_MAX;
    }
    unsigned const payload_len = header[0] * 16U + header[1];
    unsigned const has_crc = header[2] & 1;
    unsigned const coding_rate = header[2] >> 1;
    if (!payload_len || payload_len > LORA_MAX_PAYLOAD_LEN || !has_crc
            || coding_rate < 1 || coding_rate > 2) {
        return UINT_MAX;
    }
    unsigned const expected_symbols = payload_symbol_count(payload_len,
            spreading_factor, coding_rate, has_crc, low_data_rate);
    if (symbol_count < expected_symbols) {
        return UINT_MAX;
    }

    unsigned errors = 0;
    unsigned const redundancy = coding_rate + 4;
    unsigned const payload_ppm = spreading_factor - 2 * low_data_rate;
    for (unsigned offset = 8; offset + redundancy <= expected_symbols;
            offset += redundancy) {
        deinterleave(&gray[offset], redundancy, payload_ppm, codewords);
        for (unsigned i = 0; i < payload_ppm; ++i) {
            uint8_t const word = codewords[i];
            if (coding_rate == 1) {
                errors += (unsigned)parity8(word & 0x1f);
            }
            else {
                unsigned const b1 = word >> 0 & 1;
                unsigned const b2 = word >> 1 & 1;
                unsigned const b3 = word >> 2 & 1;
                unsigned const b4 = word >> 3 & 1;
                unsigned const b5 = word >> 4 & 1;
                unsigned const b6 = word >> 5 & 1;
                errors += b5 ^ b3 ^ b2 ^ b1;
                errors += b6 ^ b4 ^ b3 ^ b2;
            }
        }
    }
    return errors;
}

typedef struct {
    uint16_t symbols[LORA_MAX_SYMBOLS];
    lora_symbol_alternative_t alternatives[2 * LORA_MAX_SYMBOLS];
    unsigned alternative_count;
    unsigned symbol_count;
    unsigned spreading_factor;
    uint32_t bandwidth;
    unsigned low_data_rate;
    unsigned trials;
    unsigned max_trials;
    unsigned nodes;
    unsigned max_nodes;
    lora_packet_t *packet;
} lora_repair_state_t;

static int repair_symbol_search(lora_repair_state_t *state, unsigned first,
        unsigned current_errors, unsigned depth)
{
    unsigned const mask = (1U << state->spreading_factor) - 1;
    for (unsigned i = first; i < state->alternative_count
            && state->trials < state->max_trials
            && state->nodes < state->max_nodes; ++i) {
        state->nodes += 1;
        lora_symbol_alternative_t const alternative =
                state->alternatives[i];
        uint16_t const original = state->symbols[alternative.index];
        state->symbols[alternative.index] = (uint16_t)((original + mask + 1
                    + alternative.delta) & mask);
        unsigned const errors = payload_fec_errors(state->symbols,
                state->symbol_count, state->spreading_factor,
                state->low_data_rate);
        if (errors < current_errors) {
            unsigned next = i + 1;
            while (next < state->alternative_count
                    && state->alternatives[next].index
                        == alternative.index) {
                next += 1;
            }
            if (!errors) {
                state->trials += 1;
                if (lora_decode_symbols_mode(state->symbols,
                            state->symbol_count, state->spreading_factor,
                            state->bandwidth, state->low_data_rate,
                            state->packet, NULL) == LORA_SYMBOLS_VALID) {
                    return 1;
                }
            }
            else if (depth < LORA_REPAIR_MAX_CHANGES
                    && repair_symbol_search(state, next, errors,
                        depth + 1)) {
                return 1;
            }
        }
        state->symbols[alternative.index] = original;
    }
    return 0;
}

static int repair_symbols_mode(uint16_t const *symbols, unsigned symbol_count,
        unsigned spreading_factor, uint32_t bandwidth,
        unsigned low_data_rate, lora_packet_t *packet, unsigned max_trials)
{
    unsigned const errors = payload_fec_errors(symbols, symbol_count,
            spreading_factor, low_data_rate);
    if (!errors || errors == UINT_MAX) {
        return 0;
    }
    lora_repair_state_t state = {
            .symbol_count = symbol_count,
            .spreading_factor = spreading_factor,
            .bandwidth = bandwidth,
            .low_data_rate = low_data_rate,
            .max_trials = max_trials,
            .max_nodes = max_trials > UINT_MAX / 8
                    ? UINT_MAX : max_trials * 8,
            .packet = packet,
    };
    memcpy(state.symbols, symbols, symbol_count * sizeof(*symbols));
    unsigned const mask = (1U << spreading_factor) - 1;
    for (unsigned i = 8; i < symbol_count; ++i) {
        uint16_t const original = state.symbols[i];
        for (int delta = -1; delta <= 1; delta += 2) {
            state.symbols[i] = (uint16_t)((original + mask + 1 + delta)
                    & mask);
            if (payload_fec_errors(state.symbols, symbol_count,
                        spreading_factor, low_data_rate) < errors) {
                state.alternatives[state.alternative_count++] =
                        (lora_symbol_alternative_t){(uint16_t)i,
                            (int8_t)delta};
            }
        }
        state.symbols[i] = original;
    }
    return repair_symbol_search(&state, 0, errors, 1);
}

int lora_decode_symbols_repaired(uint16_t const *symbols,
        unsigned symbol_count, unsigned spreading_factor, uint32_t bandwidth,
        lora_packet_t *packet, unsigned max_trials)
{
    if (!symbols || !packet || !max_trials || symbol_count < 8
            || symbol_count > LORA_MAX_SYMBOLS
            || spreading_factor < LORA_MIN_SF
            || spreading_factor > LORA_MAX_SF || !bandwidth) {
        return 0;
    }
    unsigned const preferred_mode = bandwidth
            <= (((1U << spreading_factor) * 1000 - 1) / 16);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        unsigned const low_data_rate = preferred_mode ^ attempt;
        if (lora_decode_symbols_mode(symbols, symbol_count,
                    spreading_factor, bandwidth, low_data_rate, packet,
                    NULL) == LORA_SYMBOLS_VALID
                || repair_symbols_mode(symbols, symbol_count,
                    spreading_factor, bandwidth, low_data_rate, packet,
                    max_trials)) {
            return 1;
        }
    }
    return 0;
}

/** Retry a synchronized frame with fractional bins and measured clock drift.

    A zero-padded FFT interpolates the peak while keeping this slower path out
    of parameter analysis and ordinary CRC-valid packets. The PHY CRC selects
    among the remaining eighth-bin phase ambiguities.
*/
static int decode_refined_symbols(lora_fft_demod_t *demod,
        size_t preamble_offset, size_t data_offset, unsigned symbol_count,
        uint32_t bandwidth, unsigned low_data_rate, lora_packet_t *packet)
{
    unsigned const fft_len = demod->fft_len;
    if (preamble_offset < 7 * fft_len) {
        return 0;
    }

    int32_t const period = (int32_t)demod->chips * LORA_REFINED_BIN_SCALE;
    int32_t preamble[8];
    int32_t previous = 0;
    int32_t weighted = 0;
    for (unsigned i = 0; i < 8; ++i) {
        size_t const offset = preamble_offset - (7 - i) * fft_len;
        int32_t const peak = dechirp_peak_refined(demod, offset);
        if (peak < 0) {
            return 0;
        }
        int32_t unwrapped = peak;
        if (i) {
            while (unwrapped - previous > period / 2) {
                unwrapped -= period;
            }
            while (previous - unwrapped > period / 2) {
                unwrapped += period;
            }
        }
        preamble[i] = peak;
        previous = unwrapped;
        weighted += ((int)i * 2 - 7) * unwrapped;
    }
    /* sum((2*i-7)^2) / 2 == 84 for i=0..7. */
    int32_t const drift = weighted / 84;

    int32_t peaks[LORA_MAX_SYMBOLS];
    for (unsigned i = 0; i < symbol_count; ++i) {
        peaks[i] = dechirp_peak_refined(demod, data_offset + i * fft_len);
        if (peaks[i] < 0) {
            return 0;
        }
    }

    static int const phase_eighths[] = {
            0, -1, 1, -2, 2, -3, 3, -4, 4,
            -5, 5, -6, 6, -7, 7, -8, 8,
    };
    uint16_t symbols[LORA_MAX_SYMBOLS];
    for (unsigned attempt = 0;
            attempt < sizeof(phase_eighths) / sizeof(*phase_eighths); ++attempt) {
        int32_t const phase = phase_eighths[attempt]
                * (LORA_REFINED_BIN_SCALE / 8);
        int32_t drift_offset = drift;
        for (unsigned i = 0; i < symbol_count; ++i) {
            int32_t value = peaks[i] - preamble[7] - drift_offset + phase;
            value %= period;
            if (value < 0) {
                value += period;
            }
            symbols[i] = (uint16_t)(((value + LORA_REFINED_BIN_SCALE / 2)
                            / LORA_REFINED_BIN_SCALE)
                    & (demod->chips - 1));
            drift_offset = (drift_offset + drift) % period;
        }
        if (lora_decode_symbols_mode(symbols, symbol_count,
                    demod->spreading_factor, bandwidth, low_data_rate,
                    packet, NULL) == LORA_SYMBOLS_VALID) {
            return 1;
        }
    }
    return 0;
}

static lora_fft_demod_t *lora_fft_demod_create_with_cache(lora_fft_cache_t *cache)
{
    lora_fft_demod_t *demod = calloc(1, sizeof(*demod));
    if (!demod) {
        return NULL;
    }
    if (cache) {
        demod->fft_cache = cache;
        return demod;
    }

    demod->fft_cache = calloc(1, sizeof(*demod->fft_cache));
    if (!demod->fft_cache) {
        free(demod);
        return NULL;
    }
    demod->owns_fft_cache = 1;
    return demod;
}

lora_fft_demod_t *lora_fft_demod_create(void)
{
    return lora_fft_demod_create_with_cache(NULL);
}

void lora_fft_demod_free(lora_fft_demod_t *demod)
{
    if (demod) {
        for (unsigned i = 0; i < LORA_AUTO_STATE_COUNT; ++i) {
            lora_fft_demod_free(demod->auto_states[i]);
        }
        free(demod->samples);
        free(demod->fft);
        free(demod->dechirp);
        free(demod->refine_fft);
        if (demod->owns_fft_cache) {
            for (unsigned i = 0; i <= LORA_MAX_FFT_ORDER; ++i) {
                free(demod->fft_cache->plans[i].twiddle);
                free(demod->fft_cache->plans[i].bit_reverse);
            }
            free(demod->fft_cache);
        }
        free(demod);
    }
}

void lora_fft_demod_reset(lora_fft_demod_t *demod)
{
    if (demod) {
        demod->sample_count = 0;
        demod->sample_offset = 0;
        demod->detected_spreading_factor = 0;
        demod->detected_bandwidth = 0;
        demod->detected_state = 0;
        demod->candidate_incomplete = 0;
        for (unsigned i = 0; i < LORA_AUTO_STATE_COUNT; ++i) {
            lora_fft_demod_reset(demod->auto_states[i]);
        }
    }
}

static int reserve_samples(lora_fft_demod_t *demod, size_t count)
{
    if (count > SIZE_MAX / sizeof(*demod->samples)) {
        return 0;
    }
    if (count <= demod->capacity) {
        return 1;
    }
    size_t capacity = demod->capacity ? demod->capacity : 16384;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) {
            return 0;
        }
        capacity *= 2;
    }
    lora_sample_t *samples = realloc(demod->samples, capacity * sizeof(*samples));
    if (!samples) {
        return 0;
    }
    demod->samples = samples;
    demod->capacity = capacity;
    return 1;
}

static int append_cu8(lora_fft_demod_t *demod, uint8_t const *iq_buf,
        size_t sample_count, uint64_t sample_offset)
{
    if (demod->sample_count && demod->sample_shift) {
        lora_fft_demod_reset(demod);
    }
    if (demod->sample_count && sample_offset != demod->sample_offset + demod->sample_count) {
        lora_fft_demod_reset(demod);
    }
    if (!demod->sample_count) {
        demod->sample_offset = sample_offset;
        demod->sample_shift = 0;
    }
    if (sample_count > SIZE_MAX - demod->sample_count
            || !reserve_samples(demod, demod->sample_count + sample_count)) {
        return 0;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        demod->samples[demod->sample_count + i].re = (int16_t)iq_buf[2 * i] - 128;
        demod->samples[demod->sample_count + i].im = (int16_t)iq_buf[2 * i + 1] - 128;
    }
    demod->sample_count += sample_count;
    return 1;
}

static int append_cs16(lora_fft_demod_t *demod, int16_t const *iq_buf,
        size_t sample_count, uint64_t sample_offset)
{
    if (demod->sample_count && demod->sample_shift != 4) {
        lora_fft_demod_reset(demod);
    }
    if (demod->sample_count && sample_offset != demod->sample_offset + demod->sample_count) {
        lora_fft_demod_reset(demod);
    }
    if (!demod->sample_count) {
        demod->sample_offset = sample_offset;
        demod->sample_shift = 4;
    }
    if (sample_count > SIZE_MAX - demod->sample_count
            || !reserve_samples(demod, demod->sample_count + sample_count)) {
        return 0;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        /* Preserve 12 useful bits while bounding the unscaled FFT growth. */
        demod->samples[demod->sample_count + i].re = iq_buf[2 * i] / 16;
        demod->samples[demod->sample_count + i].im = iq_buf[2 * i + 1] / 16;
    }
    demod->sample_count += sample_count;
    return 1;
}

enum candidate_result {
    CANDIDATE_INVALID,
    CANDIDATE_VALID,
    CANDIDATE_INCOMPLETE,
};

static int adjust_sample_offset(size_t offset, int bins,
        unsigned oversampling, size_t *adjusted)
{
    unsigned const distance = bins < 0 ? 0U - (unsigned)bins : (unsigned)bins;
    size_t const delta = (size_t)oversampling * distance;
    if (bins < 0) {
        if (delta > offset) {
            return 0;
        }
        *adjusted = offset - delta;
    }
    else {
        if (delta > SIZE_MAX - offset) {
            return 0;
        }
        *adjusted = offset + delta;
    }
    return 1;
}

static enum candidate_result decode_data_at(lora_fft_demod_t *demod,
        size_t candidate, size_t preamble_offset, size_t data_offset,
        int preamble_bin, uint32_t bandwidth, unsigned detected_sync_word,
        unsigned phase_limit, lora_packet_t *packet, size_t *packet_end)
{
    unsigned const chips = demod->chips;
    unsigned const fft_len = demod->fft_len;
    unsigned const spreading_factor = demod->spreading_factor;
    uint16_t raw_symbols[LORA_MAX_SYMBOLS];
    for (unsigned i = 0; i < 8; ++i) {
        int const bin = dechirp_peak(demod, data_offset + i * fft_len, 1, NULL);
        if (bin < 0) {
            return CANDIDATE_INCOMPLETE;
        }
        raw_symbols[i] = (uint16_t)((bin - preamble_bin) & (chips - 1));
    }

    static int const phase_offsets[] = {0, -1, 1, -2, 2, -3, 3, -4, 4};
    unsigned const preferred_ldro = bandwidth <= ((chips * 1000 - 1) / 16);
    unsigned demodulated_symbols = 8;
    int incomplete = 0;
    for (unsigned ldro_attempt = 0; ldro_attempt < 2; ++ldro_attempt) {
        unsigned const low_data_rate = preferred_ldro ^ ldro_attempt;
        for (unsigned phase_attempt = 0;
                phase_attempt < sizeof(phase_offsets) / sizeof(*phase_offsets);
                ++phase_attempt) {
            int const phase = phase_offsets[phase_attempt];
            if ((unsigned)(phase < 0 ? -phase : phase) > phase_limit) {
                continue;
            }
            uint16_t symbols[LORA_MAX_SYMBOLS];
            for (unsigned i = 0; i < 8; ++i) {
                symbols[i] = (uint16_t)((raw_symbols[i] + chips + phase)
                        & (chips - 1));
            }

            uint16_t gray[8];
            uint8_t codewords[LORA_MAX_SF];
            uint8_t header[5];
            gray_code_symbols(symbols, 8, spreading_factor, low_data_rate, gray);
            deinterleave(gray, 8, spreading_factor - 2, codewords);
            for (unsigned i = 0; i < 5; ++i) {
                header[i] = hamming_decode(codewords[i], 8);
            }
            if (!header_checksum_valid(header)) {
                continue;
            }
            unsigned const payload_len = header[0] * 16U + header[1];
            unsigned const has_crc = header[2] & 1;
            unsigned const coding_rate = header[2] >> 1;
            if (!payload_len || payload_len > LORA_MAX_PAYLOAD_LEN || !has_crc
                    || coding_rate < 1 || coding_rate > 4) {
                continue;
            }
            unsigned const symbol_count = payload_symbol_count(payload_len,
                    spreading_factor, coding_rate, has_crc, low_data_rate);
            if (symbol_count > LORA_MAX_SYMBOLS) {
                continue;
            }
            if (data_offset > demod->sample_count
                    || symbol_count * fft_len > demod->sample_count - data_offset) {
                incomplete = 1;
                continue;
            }
            for (unsigned i = demodulated_symbols; i < symbol_count; ++i) {
                int const bin = dechirp_peak(demod,
                        data_offset + i * fft_len, 1, NULL);
                raw_symbols[i] = (uint16_t)((bin - preamble_bin) & (chips - 1));
            }
            if (symbol_count > demodulated_symbols) {
                demodulated_symbols = symbol_count;
            }
            for (unsigned i = 8; i < symbol_count; ++i) {
                symbols[i] = (uint16_t)((raw_symbols[i] + chips + phase)
                        & (chips - 1));
            }

            lora_packet_t decoded = {0};
            if (lora_decode_symbols_mode(symbols, symbol_count,
                        spreading_factor, bandwidth, low_data_rate, &decoded,
                        NULL) != LORA_SYMBOLS_VALID
                    && !decode_refined_symbols(demod, preamble_offset,
                        data_offset, symbol_count, bandwidth, low_data_rate,
                        &decoded)) {
                continue;
            }

            *packet = decoded;
            packet->sync_word = detected_sync_word;
            size_t const packet_start = candidate > 5 * fft_len
                    ? candidate - 5 * fft_len : 0;
            packet->start_offset = demod->sample_offset + packet_start;
            packet->end_offset = demod->sample_offset + data_offset
                    + symbol_count * fft_len;
            *packet_end = data_offset + symbol_count * fft_len;
            return CANDIDATE_VALID;
        }
    }
    return incomplete ? CANDIDATE_INCOMPLETE : CANDIDATE_INVALID;
}

/** Synchronize from the two network-ID upchirps and the fixed 2.25-symbol SFD.

    This avoids interpreting carrier-frequency offset in a downchirp FFT bin
    as a sample-time correction. The PHY header and payload CRC select the
    remaining small integer-bin ambiguity.
*/
static enum candidate_result decode_from_sync(lora_fft_demod_t *demod,
        size_t candidate, uint32_t bandwidth, unsigned requested_sync_word,
        lora_packet_t *packet, size_t *packet_end)
{
    unsigned const chips = demod->chips;
    unsigned const fft_len = demod->fft_len;
    int bins[10];
    for (unsigned i = 0; i < 10; ++i) {
        bins[i] = dechirp_peak(demod, candidate + i * fft_len, 1, NULL);
        if (bins[i] < 0) {
            return CANDIDATE_INCOMPLETE;
        }
    }

    unsigned const sync_step = 8;
    int incomplete = 0;
    for (unsigned i = 1; i + 1 < 10; ++i) {
        unsigned const high_delta = (bins[i] - bins[0]) & (chips - 1);
        unsigned const low_delta = (bins[i + 1] - bins[0]) & (chips - 1);
        unsigned const sync_high = ((high_delta + sync_step / 2) / sync_step) & 0x0f;
        unsigned const sync_low = ((low_delta + sync_step / 2) / sync_step) & 0x0f;
        unsigned const detected_sync_word = sync_high << 4 | sync_low;
        if (circular_bin_distance(high_delta, sync_high * sync_step, chips) > 2
                || circular_bin_distance(low_delta, sync_low * sync_step, chips) > 2
                || (!requested_sync_word && !detected_sync_word)
                || (requested_sync_word
                    && detected_sync_word != requested_sync_word)) {
            continue;
        }

        size_t const preamble_offset = candidate + (i - 1) * fft_len;
        size_t const data_offset = candidate + i * fft_len
                + 17 * fft_len / 4;
        int const preamble_bin = dechirp_peak(demod, preamble_offset, 1, NULL);
        if (preamble_bin < 0) {
            return CANDIDATE_INCOMPLETE;
        }
        enum candidate_result const result = decode_data_at(demod, candidate,
                preamble_offset, data_offset, preamble_bin, bandwidth,
                detected_sync_word, 4, packet, packet_end);
        if (result == CANDIDATE_VALID) {
            return result;
        }
        incomplete |= result == CANDIDATE_INCOMPLETE;
    }
    return incomplete ? CANDIDATE_INCOMPLETE : CANDIDATE_INVALID;
}

static enum candidate_result decode_candidate(lora_fft_demod_t *demod,
        size_t candidate, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packet, size_t *packet_end)
{
    unsigned const chips = demod->chips;
    unsigned const fft_len = demod->fft_len;
    unsigned const oversampling = demod->oversampling;
    enum candidate_result const sync_result = decode_from_sync(demod,
            candidate, bandwidth, sync_word, packet, packet_end);
    if (sync_result != CANDIDATE_INVALID) {
        return sync_result;
    }

    size_t x = candidate;
    int found_downchirp = 0;
    for (unsigned i = 0; i < 10; ++i) {
        uint32_t up_magnitude;
        uint32_t down_magnitude;
        if (dechirp_peak(demod, x, 1, &up_magnitude) < 0
                || dechirp_peak(demod, x, 0, &down_magnitude) < 0) {
            return CANDIDATE_INCOMPLETE;
        }
        x += fft_len;
        if (down_magnitude > up_magnitude) {
            found_downchirp = 1;
            break;
        }
    }
    if (!found_downchirp) {
        return CANDIDATE_INVALID;
    }

    int down_bin = dechirp_peak(demod, x, 0, NULL);
    if (down_bin < 0) {
        return CANDIDATE_INCOMPLETE;
    }
    int const signed_bin = down_bin > (int)chips / 2 ? down_bin - (int)chips : down_bin;
    if (!adjust_sample_offset(x, signed_bin, oversampling, &x)
            || x < 4 * fft_len) {
        return CANDIDATE_INVALID;
    }

    size_t const preamble_offset = x - 4 * fft_len;
    int const preamble_bin = dechirp_peak(demod, preamble_offset, 1, NULL);
    uint32_t up_magnitude;
    uint32_t down_magnitude;
    if (preamble_bin < 0
            || dechirp_peak(demod, x - fft_len, 1, &up_magnitude) < 0
            || dechirp_peak(demod, x - fft_len, 0, &down_magnitude) < 0) {
        return CANDIDATE_INCOMPLETE;
    }
    x += down_magnitude < up_magnitude ? 9 * fft_len / 4 : 5 * fft_len / 4;

    int const netid_high = dechirp_peak(demod, x - 17 * fft_len / 4, 1, NULL);
    int const netid_low = dechirp_peak(demod, x - 13 * fft_len / 4, 1, NULL);
    if (netid_high < 0 || netid_low < 0) {
        return CANDIDATE_INCOMPLETE;
    }
    unsigned const sync_step = 8;
    unsigned const high_delta = (netid_high - preamble_bin) & (chips - 1);
    unsigned const low_delta = (netid_low - preamble_bin) & (chips - 1);
    unsigned const sync_high = ((high_delta + sync_step / 2) / sync_step) & 0x0f;
    unsigned const sync_low = ((low_delta + sync_step / 2) / sync_step) & 0x0f;
    unsigned const detected_sync_word = sync_high << 4 | sync_low;
    if (circular_bin_distance(high_delta, sync_high * sync_step, chips) > 1
            || circular_bin_distance(low_delta, sync_low * sync_step, chips) > 1
            || (sync_word && detected_sync_word != sync_word)) {
        return CANDIDATE_INVALID;
    }

    return decode_data_at(demod, candidate, preamble_offset, x,
            preamble_bin, bandwidth, detected_sync_word, 0, packet,
            packet_end);
}

static int process_samples(lora_fft_demod_t *demod, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets)
{
    unsigned const fft_len = demod->fft_len;
    unsigned packet_count = 0;
    unsigned matching = 0;
    int incomplete = 0;
    int previous_bin = -1;
    size_t keep_from = demod->sample_count > LORA_KEEP_SYMBOLS * fft_len
            ? demod->sample_count - LORA_KEEP_SYMBOLS * fft_len : 0;

    for (size_t offset = 0; offset + 6 * fft_len <= demod->sample_count;
            offset += fft_len) {
        int const bin = dechirp_peak(demod, offset, 1, NULL);
        if (previous_bin >= 0
                && circular_bin_distance((unsigned)bin, (unsigned)previous_bin,
                    demod->chips) <= 2) {
            matching += 1;
        }
        else {
            matching = 1;
        }
        previous_bin = bin;

        if (matching < LORA_PREAMBLE_MATCHES) {
            continue;
        }
        size_t const coarse = offset + fft_len;
        if ((size_t)(demod->oversampling * bin) > coarse) {
            matching = 0;
            continue;
        }
        size_t const candidate = coarse - demod->oversampling * bin;
        lora_packet_t packet = {0};
        size_t packet_end = 0;
        enum candidate_result const result = decode_candidate(demod, candidate,
                bandwidth, sync_word, &packet, &packet_end);
        if (result == CANDIDATE_INCOMPLETE) {
            keep_from = 0;
            incomplete = 1;
            matching = 0;
            previous_bin = -1;
            continue;
        }
        if (result == CANDIDATE_VALID) {
            packet.bandwidth = bandwidth;
            if (packet_count < max_packets) {
                packets[packet_count++] = packet;
            }
            offset = packet_end > fft_len ? packet_end - fft_len : offset;
        }
        matching = 0;
        previous_bin = -1;
    }

    /* Keep the dechirp phase grid stable across streaming input blocks. */
    keep_from -= keep_from % fft_len;
    if (keep_from > 0) {
        size_t const remaining = demod->sample_count - keep_from;
        memmove(demod->samples, demod->samples + keep_from, remaining * sizeof(*demod->samples));
        demod->sample_count = remaining;
        demod->sample_offset += keep_from;
    }
    demod->candidate_incomplete = incomplete;
    return (int)packet_count;
}

static uint32_t abs_i32(int32_t value)
{
    return value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
}

static uint32_t correlation_quality(int32_t re, int32_t im,
        uint32_t power_a, uint32_t power_b)
{
    uint32_t scale = abs_i32(re);
    if (abs_i32(im) > scale) {
        scale = abs_i32(im);
    }
    if (power_a > scale) {
        scale = power_a;
    }
    if (power_b > scale) {
        scale = power_b;
    }
    while (scale > INT16_MAX) {
        re >>= 1;
        im >>= 1;
        power_a >>= 1;
        power_b >>= 1;
        scale >>= 1;
    }

    uint32_t const abs_re = abs_i32(re);
    uint32_t const abs_im = abs_i32(im);
    uint32_t numerator = abs_re * abs_re + abs_im * abs_im;
    uint32_t denominator = power_a * power_b;
    if (!denominator || numerator > denominator) {
        return numerator ? 65535 : 0;
    }
    while (numerator > UINT32_MAX / 65535) {
        numerator >>= 1;
        denominator >>= 1;
    }
    if (!denominator) {
        return 0;
    }
    uint32_t const quality = numerator * 65535 / denominator;
    return (uint32_t)(quality < 65535 ? quality : 65535);
}

static uint32_t repeated_symbol_quality(lora_fft_demod_t const *demod,
        size_t offset, unsigned symbol_len)
{
    unsigned const stride = symbol_len > 128 ? (symbol_len + 127) / 128 : 1;
    unsigned const shift_limit = symbol_len / 2048 < 16
            ? symbol_len / 2048 : 16;
    int const scale = 1 << demod->sample_shift;
    uint32_t best = 0;
    for (int sample_shift = -(int)shift_limit;
            sample_shift <= (int)shift_limit; ++sample_shift) {
        int32_t re = 0;
        int32_t im = 0;
        uint32_t power_a = 0;
        uint32_t power_b = 0;
        unsigned const first = sample_shift < 0 ? (unsigned)-sample_shift : 0;
        unsigned const end = sample_shift > 0
                ? symbol_len - (unsigned)sample_shift : symbol_len;
        for (unsigned pair = 0; pair + 1 < LORA_ANALYSIS_SYMBOLS; ++pair) {
            size_t const a_offset = offset + pair * symbol_len;
            size_t const b_offset = a_offset + symbol_len;
            for (unsigned n = first; n < end; n += stride) {
                int32_t const ar = demod->samples[a_offset + n].re / scale;
                int32_t const ai = demod->samples[a_offset + n].im / scale;
                size_t const shifted = (size_t)((int)n + sample_shift);
                int32_t const br = demod->samples[b_offset + shifted].re / scale;
                int32_t const bi = demod->samples[b_offset + shifted].im / scale;
                re += ar * br + ai * bi;
                im += ar * bi - ai * br;
                power_a += (uint32_t)(ar * ar + ai * ai);
                power_b += (uint32_t)(br * br + bi * bi);
            }
        }
        uint32_t const quality = correlation_quality(re, im, power_a, power_b);
        if (quality > best) {
            best = quality;
        }
    }
    return best;
}

static lora_fft_demod_t *auto_state(lora_fft_demod_t *demod,
        unsigned spreading_factor, unsigned bandwidth_index)
{
    unsigned const index = (spreading_factor - LORA_MIN_SF)
            * LORA_AUTO_BANDWIDTH_COUNT + bandwidth_index;
    if (!demod->auto_states[index]) {
        demod->auto_states[index] = lora_fft_demod_create_with_cache(demod->fft_cache);
    }
    return demod->auto_states[index];
}

static int analyze_parameters(lora_fft_demod_t *demod, uint32_t sample_rate,
        unsigned requested_sf, uint32_t requested_bandwidth)
{
    uint32_t best_quality = 0;
    unsigned best_sf = 0;
    uint32_t best_bandwidth = 0;
    unsigned best_state = 0;
    unsigned configured = 0;
    size_t max_window = 0;
    uint32_t repeat_qualities[LORA_MAX_FFT_ORDER + 1] = {0};
    size_t repeat_offsets[LORA_MAX_FFT_ORDER + 1] = {0};
    uint8_t repeat_evaluated[LORA_MAX_FFT_ORDER + 1] = {0};

    unsigned const sf_first = requested_sf ? requested_sf : LORA_MIN_SF;
    unsigned const sf_last = requested_sf ? requested_sf : LORA_MAX_SF;
    unsigned const bandwidth_count = requested_bandwidth ? 1 : LORA_AUTO_BANDWIDTH_COUNT;
    for (unsigned sf = sf_first; sf <= sf_last; ++sf) {
        for (unsigned bandwidth_index = 0; bandwidth_index < bandwidth_count; ++bandwidth_index) {
            uint32_t const bandwidth = requested_bandwidth
                    ? requested_bandwidth : auto_bandwidths[bandwidth_index];
            lora_fft_demod_t *candidate = auto_state(demod, sf, bandwidth_index);
            if (!candidate) {
                return -2;
            }
            if (!configure_phy(candidate, sf, sample_rate, bandwidth)) {
                continue;
            }
            configured += 1;
            size_t const window = LORA_ANALYSIS_SYMBOLS * candidate->fft_len;
            if (window > max_window) {
                max_window = window;
            }
            if (demod->sample_count < window) {
                continue;
            }

            unsigned const fft_order = fft_fixed_order(candidate->fft_len);
            if (!repeat_evaluated[fft_order]) {
                unsigned const step = candidate->fft_len / 2;
                for (size_t offset = 0;
                        offset + window <= demod->sample_count; offset += step) {
                    uint32_t const quality = repeated_symbol_quality(demod,
                            offset, candidate->fft_len);
                    if (quality > repeat_qualities[fft_order]) {
                        repeat_qualities[fft_order] = quality;
                        repeat_offsets[fft_order] = offset;
                    }
                }
                repeat_evaluated[fft_order] = 1;
            }
            uint32_t const repeat_quality = repeat_qualities[fft_order];
            size_t const repeat_offset = repeat_offsets[fft_order];
            if (repeat_quality < 30000) {
                continue;
            }
            size_t const refine_start = repeat_offset > candidate->fft_len
                    ? repeat_offset - candidate->fft_len : 0;
            size_t const refine_end = repeat_offset + candidate->fft_len < demod->sample_count - window
                    ? repeat_offset + candidate->fft_len : demod->sample_count - window;
            uint32_t concentration = 0;
            unsigned const fft_step = candidate->fft_len > 2
                    ? candidate->fft_len / 2 : 1;
            for (size_t offset = refine_start; offset <= refine_end; offset += fft_step) {
                int peak_bin = 0;
                uint32_t quality = analyzer_dechirp_quality(demod,
                        candidate, offset, &peak_bin);
                int const signed_bin = peak_bin > (int)candidate->chips / 2
                        ? peak_bin - (int)candidate->chips : peak_bin;
                size_t aligned;
                if (adjust_sample_offset(offset, -signed_bin,
                            candidate->oversampling, &aligned)) {
                    uint32_t const aligned_quality = analyzer_dechirp_quality(demod,
                            candidate, aligned, NULL);
                    if (aligned_quality > quality) {
                        quality = aligned_quality;
                    }
                }
                if (quality > concentration) {
                    concentration = quality;
                }
            }
            if (concentration < 12000) {
                continue;
            }
            uint32_t const candidate_quality = concentration < repeat_quality
                    ? concentration : repeat_quality;
            if (candidate_quality && (candidate_quality > best_quality
                        || (best_quality - candidate_quality <= 128
                                && bandwidth > best_bandwidth))) {
                best_quality = candidate_quality;
                best_sf = sf;
                best_bandwidth = bandwidth;
                best_state = (sf - LORA_MIN_SF) * LORA_AUTO_BANDWIDTH_COUNT
                        + bandwidth_index;
            }
        }
    }

    if (best_sf) {
        demod->detected_spreading_factor = best_sf;
        demod->detected_bandwidth = best_bandwidth;
        demod->detected_state = best_state;
        return 1;
    }
    if (max_window && demod->sample_count > 2 * max_window) {
        size_t const keep_from = demod->sample_count - max_window;
        memmove(demod->samples, demod->samples + keep_from,
                max_window * sizeof(*demod->samples));
        demod->sample_count = max_window;
        demod->sample_offset += keep_from;
    }
    return configured ? 0 : -1;
}

static int append_samples(lora_fft_demod_t *demod, lora_fft_demod_t const *source)
{
    if (demod->sample_count
            && source->sample_offset != demod->sample_offset + demod->sample_count) {
        lora_fft_demod_reset(demod);
    }
    if (!demod->sample_count) {
        demod->sample_offset = source->sample_offset;
        demod->sample_shift = source->sample_shift;
    }
    if (source->sample_count > SIZE_MAX - demod->sample_count
            || !reserve_samples(demod, demod->sample_count + source->sample_count)) {
        return 0;
    }
    memcpy(demod->samples + demod->sample_count, source->samples,
            source->sample_count * sizeof(*source->samples));
    demod->sample_count += source->sample_count;
    return 1;
}

static void clear_detection(lora_fft_demod_t *demod)
{
    if (demod->detected_spreading_factor) {
        lora_fft_demod_reset(demod->auto_states[demod->detected_state]);
    }
    demod->detected_spreading_factor = 0;
    demod->detected_bandwidth = 0;
    demod->detected_state = 0;
}

static int same_chirp_slope(unsigned sf_a, uint32_t bandwidth_a,
        unsigned sf_b, uint32_t bandwidth_b)
{
    if (!bandwidth_a || !bandwidth_b) {
        return 0;
    }
    unsigned exponent_a = 0;
    unsigned exponent_b = 0;
    while (!(bandwidth_a & 1)) {
        bandwidth_a >>= 1;
        exponent_a += 1;
    }
    while (!(bandwidth_b & 1)) {
        bandwidth_b >>= 1;
        exponent_b += 1;
    }
    return bandwidth_a == bandwidth_b
            && 2 * exponent_a + sf_b == 2 * exponent_b + sf_a;
}

static int process_alternates(lora_fft_demod_t *demod, unsigned selected_state,
        unsigned selected_sf, uint32_t selected_bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets)
{
    unsigned incomplete_state = LORA_AUTO_STATE_COUNT;
    unsigned incomplete_sf = 0;
    uint32_t incomplete_bandwidth = 0;
    /* Equal-slope aliases are the most likely analyzer ambiguity. If none
       validates, let the PHY CRC arbitrate the remaining configured states. */
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned sf = LORA_MIN_SF; sf <= LORA_MAX_SF; ++sf) {
            for (unsigned bandwidth_index = 0;
                    bandwidth_index < LORA_AUTO_BANDWIDTH_COUNT; ++bandwidth_index) {
                unsigned const state_index = (sf - LORA_MIN_SF)
                        * LORA_AUTO_BANDWIDTH_COUNT + bandwidth_index;
                uint32_t const bandwidth = auto_bandwidths[bandwidth_index];
                int const is_alias = same_chirp_slope(sf, bandwidth,
                        selected_sf, selected_bandwidth);
                if (state_index == selected_state
                        || (!pass && !is_alias) || (pass && is_alias)) {
                    continue;
                }
                lora_fft_demod_t *candidate = demod->auto_states[state_index];
                if (!candidate || candidate->spreading_factor != sf
                        || candidate->bandwidth != bandwidth) {
                    continue;
                }
                lora_fft_demod_reset(candidate);
                if (!append_samples(candidate, demod)) {
                    return -2;
                }
                int const result = process_samples(candidate, bandwidth,
                        sync_word, packets, max_packets);
                if (result > 0) {
                    demod->detected_spreading_factor = sf;
                    demod->detected_bandwidth = bandwidth;
                    demod->detected_state = state_index;
                    return result;
                }
                if (!pass && candidate->candidate_incomplete
                        && incomplete_state == LORA_AUTO_STATE_COUNT) {
                    incomplete_state = state_index;
                    incomplete_sf = sf;
                    incomplete_bandwidth = bandwidth;
                }
            }
        }
    }
    if (incomplete_state != LORA_AUTO_STATE_COUNT) {
        demod->detected_spreading_factor = incomplete_sf;
        demod->detected_bandwidth = incomplete_bandwidth;
        demod->detected_state = incomplete_state;
    }
    return 0;
}

static int process_analyzed(lora_fft_demod_t *demod, uint32_t sample_rate,
        unsigned requested_sf, uint32_t requested_bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets)
{
    unsigned const previous_sf = demod->detected_spreading_factor;
    unsigned const previous_state = demod->detected_state;
    int const analysis = previous_sf ? 1
            : analyze_parameters(demod, sample_rate,
                    requested_sf, requested_bandwidth);
    if (analysis < 0) {
        return analysis;
    }
    if (!demod->detected_spreading_factor) {
        return 0;
    }

    lora_fft_demod_t *selected = demod->auto_states[demod->detected_state];
    if (!previous_sf || previous_state != demod->detected_state) {
        if (previous_sf) {
            lora_fft_demod_reset(demod->auto_states[previous_state]);
        }
        lora_fft_demod_reset(selected);
        if (!append_samples(selected, demod)) {
            return -2;
        }
    }
    else {
        uint64_t const selected_end = selected->sample_offset + selected->sample_count;
        uint64_t const source_end = demod->sample_offset + demod->sample_count;
        if (selected_end < demod->sample_offset || selected_end > source_end) {
            lora_fft_demod_reset(selected);
            if (!append_samples(selected, demod)) {
                return -2;
            }
        }
        else if (selected_end < source_end) {
            size_t const source_index = (size_t)(selected_end - demod->sample_offset);
            lora_fft_demod_t source = {0};
            source.samples = demod->samples + source_index;
            source.sample_count = demod->sample_count - source_index;
            source.sample_offset = selected_end;
            source.sample_shift = demod->sample_shift;
            if (!append_samples(selected, &source)) {
                return -2;
            }
        }
    }
    unsigned const selected_sf = demod->detected_spreading_factor;
    uint32_t const selected_bandwidth = demod->detected_bandwidth;
    unsigned const selected_state = demod->detected_state;
    int result = process_samples(selected, selected_bandwidth,
            sync_word, packets, max_packets);
    if (!result && !requested_sf && !requested_bandwidth) {
        result = process_alternates(demod, selected_state, selected_sf,
                selected_bandwidth, sync_word, packets, max_packets);
    }
    if (result > 0) {
        uint64_t consumed_until = demod->sample_offset;
        for (int i = 0; i < result; ++i) {
            if (packets[i].end_offset > consumed_until) {
                consumed_until = packets[i].end_offset;
            }
        }
        size_t const consumed = consumed_until - demod->sample_offset < demod->sample_count
                ? (size_t)(consumed_until - demod->sample_offset) : demod->sample_count;
        memmove(demod->samples, demod->samples + consumed,
                (demod->sample_count - consumed) * sizeof(*demod->samples));
        demod->sample_count -= consumed;
        demod->sample_offset += consumed;
        clear_detection(demod);
    }
    else if (demod->detected_state == selected_state
            && !selected->candidate_incomplete) {
        clear_detection(demod);
    }
    if (!result) {
        size_t const keep = LORA_ANALYSIS_SYMBOLS * LORA_MAX_OVERSAMPLING
                * (1U << LORA_MAX_SF);
        if (demod->sample_count > keep) {
            size_t const drop = demod->sample_count - keep;
            memmove(demod->samples, demod->samples + drop,
                    keep * sizeof(*demod->samples));
            demod->sample_count = keep;
            demod->sample_offset += drop;
        }
    }
    return result;
}

int lora_fft_demod_process_cu8(lora_fft_demod_t *demod, uint8_t const *iq_buf,
        size_t sample_count, uint64_t sample_offset, uint32_t sample_rate,
        unsigned spreading_factor, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets)
{
    if (!demod || !iq_buf || (!packets && max_packets)) {
        return -2;
    }
    if (spreading_factor && bandwidth) {
        if (!configure_phy(demod, spreading_factor, sample_rate, bandwidth)) {
            return -1;
        }
        if (!append_cu8(demod, iq_buf, sample_count, sample_offset)) {
            return -2;
        }
        return process_samples(demod, bandwidth, sync_word, packets, max_packets);
    }

    if (!append_cu8(demod, iq_buf, sample_count, sample_offset)) {
        return -2;
    }
    return process_analyzed(demod, sample_rate, spreading_factor, bandwidth,
            sync_word, packets, max_packets);
}

int lora_fft_demod_process_cs16(lora_fft_demod_t *demod, int16_t const *iq_buf,
        size_t sample_count, uint64_t sample_offset, uint32_t sample_rate,
        unsigned spreading_factor, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets)
{
    if (!demod || !iq_buf || (!packets && max_packets)) {
        return -2;
    }
    if (spreading_factor && bandwidth) {
        if (!configure_phy(demod, spreading_factor, sample_rate, bandwidth)) {
            return -1;
        }
        if (!append_cs16(demod, iq_buf, sample_count, sample_offset)) {
            return -2;
        }
        return process_samples(demod, bandwidth, sync_word, packets, max_packets);
    }

    if (!append_cs16(demod, iq_buf, sample_count, sample_offset)) {
        return -2;
    }
    return process_analyzed(demod, sample_rate, spreading_factor, bandwidth,
            sync_word, packets, max_packets);
}

#ifdef _TEST
#include <stdio.h>

static int check_symbols(char const *name, uint16_t const *symbols,
        unsigned symbol_count, unsigned spreading_factor, unsigned coding_rate,
        uint8_t const *expected, unsigned expected_len)
{
    lora_packet_t packet = {0};
    if (!lora_decode_symbols(symbols, symbol_count,
                spreading_factor, 125000, &packet)) {
        fprintf(stderr, "lora: %s failed to decode\n", name);
        return 0;
    }
    if (packet.payload_len != expected_len || packet.coding_rate != coding_rate
            || memcmp(packet.payload, expected, expected_len)) {
        fprintf(stderr, "lora: %s payload mismatch\n", name);
        return 0;
    }
    return 1;
}

static int check_fft_plan_cache(void)
{
    lora_fft_demod_t *demod = lora_fft_demod_create();
    if (!demod) {
        return 0;
    }
    int valid = configure_phy(demod, 7, 1000000, 125000);
    lora_fft_plan_t *shared = demod->fft_plan;
    valid = valid && configure_phy(demod, 7, 1000000, 250000)
            && demod->fft_plan == shared && prepare_refined_fft(demod);
    shared = demod->refine_plan;
    valid = valid && configure_phy(demod, 11, 1000000, 125000)
            && demod->fft_plan == shared;
    lora_fft_demod_free(demod);
    return valid;
}

int main(void)
{
    uint16_t const cotech_symbols[] = {
            109, 9, 97, 61, 29, 109, 57, 121,
            50, 122, 9, 20, 24, 26, 101, 38, 109, 3, 56, 21,
            55, 96, 29, 52, 28, 5, 113, 103, 44, 51, 43, 72, 19,
    };
    uint8_t const cotech_expected[] = {
            0xd4, 0xce, 0xa0, 0x00, 0x00, 0x37, 0x00, 0x09,
            0x34, 0x61, 0x2f, 0xff, 0xfb, 0xfb, 0x45,
    };
    uint8_t const expected[] = {
            0x40, 0xda, 0x1b, 0x01, 0x26, 0x80, 0x34, 0x12,
            0x01, 0xaa, 0x55, 0x10, 0x20, 0x30, 0x40,
    };
    uint16_t const sf7[] = {
            109, 9, 97, 61, 29, 109, 57, 121, 99, 106, 55, 41, 94, 119, 82, 87,
            112, 65, 35, 93, 57, 76, 14, 31, 127, 94, 59, 44, 115, 38, 23, 50, 111,
    };
    uint16_t const sf8[] = {
            145, 121, 197, 97, 53, 37, 145, 69, 79, 181, 220, 53, 188, 222, 94, 108,
            118, 45, 54, 253, 28, 21, 216, 154, 247, 3, 111, 48, 50, 77, 145, 41,
            172, 171, 171, 150, 182, 166,
    };
    uint16_t const sf9[] = {
            273, 181, 473, 221, 141, 57, 281, 141, 296, 38, 211, 27, 163, 290, 487, 280,
            283, 318, 476, 259, 385, 41, 452, 510, 454, 396, 430, 134, 250, 327, 339,
            428, 425, 492, 246, 299,
    };
    uint16_t const sf10[] = {
            365, 185, 801, 545, 309, 153, 965, 485, 148, 750, 874, 1011, 882, 705,
            533, 763, 443, 698, 656, 874, 657, 224, 31, 399, 441, 192, 712, 562, 323,
            736, 827, 375, 684, 683, 683, 598, 726, 662, 683, 683,
    };
    uint16_t const sf11[] = {
            365, 885, 1825, 989, 433, 1753, 1125, 953, 293, 1497, 1325, 1049, 273,
            1769, 1305, 1473, 1625, 401, 573, 1533, 1477, 1653, 297, 1349, 1361,
            1705, 341, 257,
    };
    uint16_t const sf12[] = {
            3729, 1157, 3269, 2013, 841, 549, 2329, 1145, 2193, 2797, 1385, 2829,
            1165, 705, 441, 325, 653, 1429, 657, 221, 3653, 1213, 709, 1485, 2365, 669,
    };
    lora_packet_t packet = {0};

    if (!check_fft_plan_cache()) {
        fprintf(stderr, "lora: FFT plan cache failed\n");
        return 1;
    }

    if (!lora_decode_symbols(cotech_symbols,
                sizeof(cotech_symbols) / sizeof(*cotech_symbols),
                7, 500000, &packet)) {
        fprintf(stderr, "lora: failed to decode valid symbol stream\n");
        return 1;
    }
    if (packet.payload_len != sizeof(cotech_expected)
            || memcmp(packet.payload, cotech_expected, sizeof(cotech_expected))) {
        fprintf(stderr, "lora: decoded payload mismatch\n");
        return 1;
    }

    if (lora_decode_symbols(cotech_symbols,
                sizeof(cotech_symbols) / sizeof(*cotech_symbols) - 1,
                7, 500000, &packet)) {
        fprintf(stderr, "lora: accepted a truncated packet\n");
        return 1;
    }
    unsigned expected_symbols = 0;
    if (lora_decode_symbols_ex(cotech_symbols,
                sizeof(cotech_symbols) / sizeof(*cotech_symbols) - 1,
                7, 500000, &packet, &expected_symbols)
                    != LORA_SYMBOLS_INCOMPLETE
            || expected_symbols
                    != sizeof(cotech_symbols) / sizeof(*cotech_symbols)) {
        fprintf(stderr, "lora: truncated packet length was not recovered\n");
        return 1;
    }
    if (!check_symbols("SF7", sf7, sizeof(sf7) / sizeof(*sf7), 7, 1,
                expected, sizeof(expected))
            || !check_symbols("SF8", sf8, sizeof(sf8) / sizeof(*sf8), 8, 2,
                expected, sizeof(expected))
            || !check_symbols("SF9", sf9, sizeof(sf9) / sizeof(*sf9), 9, 3,
                expected, sizeof(expected))
            || !check_symbols("SF10", sf10, sizeof(sf10) / sizeof(*sf10), 10, 4,
                expected, sizeof(expected))
            || !check_symbols("SF11", sf11, sizeof(sf11) / sizeof(*sf11), 11, 1,
                expected, sizeof(expected))
            || !check_symbols("SF12", sf12, sizeof(sf12) / sizeof(*sf12), 12, 2,
                expected, sizeof(expected))) {
        return 1;
    }

    uint16_t damaged_sf7[sizeof(sf7) / sizeof(*sf7)];
    memcpy(damaged_sf7, sf7, sizeof(sf7));
    damaged_sf7[8] += 1;
    damaged_sf7[19] -= 1;
    damaged_sf7[27] += 1;
    if (lora_decode_symbols(damaged_sf7,
                sizeof(damaged_sf7) / sizeof(*damaged_sf7), 7, 125000,
                &packet)
            || !lora_decode_symbols_repaired(damaged_sf7,
                sizeof(damaged_sf7) / sizeof(*damaged_sf7), 7, 125000,
                &packet, 64)
            || packet.payload_len != sizeof(expected)
            || memcmp(packet.payload, expected, sizeof(expected))) {
        fprintf(stderr, "lora: bounded symbol repair failed\n");
        return 1;
    }

    fprintf(stderr, "lora: tests passed\n");
    return 0;
}
#endif
