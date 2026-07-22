/** @file
    Fixed-point LoRa chirp matching in instantaneous-frequency space.

    This receiver consumes the same filtered frequency discriminator used by
    the FSK minmax detector. A LoRa upchirp becomes a cyclic linear ramp in
    that domain. Subtracting an aligned ramp and folding the residual by one
    bandwidth leaves a nearly constant value proportional to the symbol.

    The receive path has four stages:
    1. The analyzer looks for four equally spaced downward frequency resets
       and tests the compatible SF/bandwidth combinations.
    2. A filtered ideal chirp establishes coarse timing. The first four
       received preamble chirps are averaged to learn the discriminator's
       actual nonlinear frequency-to-level response.
    3. The learned monotonic ramp is inverted. Each payload sample votes for
       the cyclic displacement. Three chips at symbol edges and three chips
       around a chirp reset are ignored because RF and discriminator filters
       smear those discontinuities.
    4. The explicit PHY header selects the small residual phase/clock
       correction and tells the streaming receiver exactly how many symbols
       to retain. For weak CR 4/5 and 4/6 frames, Hamming parity can nominate
       a bounded set of adjacent-bin alternatives; the payload CRC selects
       the result and remains mandatory.

    Sample-domain work is integer-only and linear in the frame length. The
    generated reference uses the exact fixed-point low-pass recurrence from
    baseband_demod_FM() for its default 0.2 cutoff.
*/

#include "lora_fm.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define LORA_FM_MIN_SF 7U
#define LORA_FM_MAX_SF 12U
#define LORA_FM_MIN_OVERSAMPLING 2U
#define LORA_FM_MAX_OVERSAMPLING 16U
#define LORA_FM_HISTOGRAM_BINS 64U
#define LORA_FM_MAX_CHIPS (1U << LORA_FM_MAX_SF)
#define LORA_FM_MAX_SYMBOLS 640U
#define LORA_FM_MAX_CLOCKS 2U
#define LORA_FM_MAX_CORRECTIONS (LORA_FM_MAX_CLOCKS * 33U * 33U)
#define LORA_FM_REPAIR_CANDIDATES 2U
#define LORA_FM_REPAIR_TRIALS 64U
#define LORA_FM_MIN_PREAMBLE_RESETS 4U
#define LORA_FM_SEARCH_SYMBOLS 12U
#define LORA_FM_SF_COUNT (LORA_FM_MAX_SF - LORA_FM_MIN_SF + 1U)
#define LORA_FM_RESET_CACHE_COUNT 4U
#define LORA_FM_ALIGNMENT_SAMPLES 512U
#define LORA_FM_EDGE_GUARD_CHIPS 3U
#define LORA_FM_RESET_GUARD_CHIPS 3U
/* baseband_demod_FM() coefficients for the minmax default low_pass=0.2. */
#define LORA_FM_FILTER_FEEDBACK 8348
#define LORA_FM_FILTER_FEEDFORWARD 4017
#define LORA_FM_FILTER_SHIFT 14

typedef struct {
    int32_t center;
    uint32_t error;
    unsigned count;
} chirp_measurement_t;

typedef struct {
    int16_t phase_adjust_q8;
    int16_t drift_adjust_q8;
    uint16_t expected_symbols;
    uint8_t clock_index;
} correction_candidate_t;

typedef struct {
    uint64_t *positions;
    size_t count;
    size_t capacity;
    size_t tested[LORA_FM_SF_COUNT];
    uint64_t scan_offset;
    uint32_t bandwidth;
    unsigned guard;
    int32_t span;
} reset_cache_t;

struct lora_fm_demod {
    /* Streaming discriminator input and reusable candidate workspaces. */
    int16_t *samples;
    size_t sample_count;
    size_t capacity;
    int16_t *reference;
    unsigned reference_capacity;
    unsigned reference_period;
    int32_t reference_span;
    int16_t *preamble_template;
    unsigned template_capacity;
    uint16_t *inverse_template;
    unsigned inverse_capacity;
    unsigned inverse_size;
    int32_t inverse_minimum;
    uint16_t *symbol_shifts;
    unsigned shift_capacity;
    uint16_t histogram[LORA_FM_MAX_CHIPS];
    uint64_t sample_offset;
    uint32_t sample_rate;
    reset_cache_t reset_cache[LORA_FM_RESET_CACHE_COUNT];
    uint32_t search_sync_word;
    int search_sync_valid;

    /* Parameters and timing of the currently synchronized frame. */
    int locked;
    unsigned spreading_factor;
    uint32_t bandwidth;
    unsigned sync_word;
    unsigned chips;
    unsigned oversampling;
    int32_t span;
    uint64_t period_q16;
    uint64_t frame_start_q16;
    uint64_t data_offset_q16;
    int32_t symbol_phase_q8[LORA_FM_MAX_CLOCKS];
    int32_t symbol_drift_q8[LORA_FM_MAX_CLOCKS];
    unsigned clock_count;
    int32_t matched_q8[LORA_FM_MAX_SYMBOLS];
    correction_candidate_t corrections[LORA_FM_MAX_CORRECTIONS];
    unsigned matched_symbol_count;
    unsigned correction_count;
    int corrections_ready;
};

/* Ordered so the most likely phase/clock corrections are tested first. */
static int const phase_eighths[] = {
        0, -1, 1, -2, 2, -3, 3, -4, 4,
        -5, 5, -6, 6, -7, 7, -8, 8,
        -9, 9, -10, 10, -11, 11, -12, 12,
        -13, 13, -14, 14, -15, 15, -16, 16,
};

static int const drift_adjustments[] = {
        0, -1, 1, -2, 2, -3, 3, -4, 4,
        -5, 5, -6, 6, -7, 7, -8, 8,
        -9, 9, -10, 10, -11, 11, -12, 12,
        -13, 13, -14, 14, -15, 15, -16, 16,
};

static int prepare_reference(lora_fm_demod_t *demod, unsigned period,
        int32_t span)
{
    if (demod->reference_period == period && demod->reference_span == span) {
        return 1;
    }
    if (period > demod->reference_capacity) {
        int16_t *reference = realloc(demod->reference, period * sizeof(*demod->reference));
        if (!reference) {
            return 0;
        }
        demod->reference = reference;
        demod->reference_capacity = period;
    }

    /* Run one unrecorded cycle to reach the periodic filter state. */
    int32_t previous_input = span / 2
            - span / (int32_t)period;
    int32_t previous_output = previous_input;
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        for (unsigned i = 0; i < period; ++i) {
            int32_t const input = -span / 2
                    + (int32_t)i * span / (int32_t)period;
            int32_t const output = (LORA_FM_FILTER_FEEDBACK * previous_output
                    + LORA_FM_FILTER_FEEDFORWARD
                        * (input + previous_input)) >> LORA_FM_FILTER_SHIFT;
            if (cycle) {
                demod->reference[i] = (int16_t)output;
            }
            previous_input = input;
            previous_output = output;
        }
    }
    demod->reference_period = period;
    demod->reference_span = span;
    return 1;
}

static uint32_t abs_i32(int32_t value)
{
    return value < 0 ? 0U - (uint32_t)value : (uint32_t)value;
}

static int32_t fold_level(int32_t value, int32_t span)
{
    if (!(span & (span - 1))) {
        uint32_t const mask = (uint32_t)span - 1;
        return (int32_t)((uint32_t)(value + span / 2) & mask) - span / 2;
    }
    value %= span;
    if (value < -span / 2) {
        value += span;
    }
    else if (value >= (span + 1) / 2) {
        value -= span;
    }
    return value;
}

static size_t q16_index(uint64_t position)
{
    return (size_t)((position + 0x8000U) >> 16);
}

static int reserve_samples(lora_fm_demod_t *demod, size_t count)
{
    if (count <= demod->capacity) {
        return 1;
    }
    if (count > SIZE_MAX / sizeof(*demod->samples)) {
        return 0;
    }
    size_t capacity = demod->capacity ? demod->capacity : 16384;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2) {
            return 0;
        }
        capacity *= 2;
    }
    int16_t *samples = realloc(demod->samples, capacity * sizeof(*demod->samples));
    if (!samples) {
        return 0;
    }
    demod->samples = samples;
    demod->capacity = capacity;
    return 1;
}

static int append_samples(lora_fm_demod_t *demod, int16_t const *samples,
        size_t count, uint64_t sample_offset, uint32_t sample_rate)
{
    if ((demod->sample_count
                && sample_offset != demod->sample_offset + demod->sample_count)
            || (demod->sample_rate && demod->sample_rate != sample_rate)) {
        lora_fm_demod_reset(demod);
    }
    if (!demod->sample_count) {
        demod->sample_offset = sample_offset;
        demod->sample_rate = sample_rate;
    }
    if (count > SIZE_MAX - demod->sample_count
            || !reserve_samples(demod, demod->sample_count + count)) {
        return 0;
    }
    memcpy(demod->samples + demod->sample_count, samples,
            count * sizeof(*samples));
    demod->sample_count += count;
    return 1;
}

static int configure_candidate(uint32_t sample_rate, unsigned sf,
        uint32_t bandwidth, unsigned *oversampling, unsigned *period,
        int32_t *span)
{
    if (sf < LORA_FM_MIN_SF || sf > LORA_FM_MAX_SF || !bandwidth
            || sample_rate % bandwidth) {
        return 0;
    }
    unsigned const os = sample_rate / bandwidth;
    if (os < LORA_FM_MIN_OVERSAMPLING || os > LORA_FM_MAX_OVERSAMPLING
            || (os & (os - 1))) {
        return 0;
    }
    *oversampling = os;
    *period = os * (1U << sf);
    *span = (int32_t)((2U * INT16_MAX + os / 2) / os);
    return 1;
}

static unsigned find_frequency_reset(lora_fm_demod_t const *demod,
        size_t offset, unsigned length, unsigned guard, int downchirp,
        int32_t span)
{
    int32_t best_delta = 0;
    unsigned reset = length;
    for (unsigned i = guard; i + guard < length; ++i) {
        int32_t const delta = (int32_t)demod->samples[offset + i]
                - demod->samples[offset + i - 1];
        if ((downchirp && delta > best_delta)
                || (!downchirp && delta < best_delta)) {
            best_delta = delta;
            reset = i;
        }
    }
    return abs_i32(best_delta) >= (uint32_t)span / 16 ? reset : length;
}

static int chirp_sample_valid(unsigned position, unsigned length,
        unsigned edge_guard, unsigned reset, unsigned reset_guard)
{
    if (position < edge_guard || position + edge_guard >= length) {
        return 0;
    }
    if (reset == length) {
        return 1;
    }
    unsigned const distance = position > reset
            ? position - reset : reset - position;
    unsigned const cyclic_distance = distance < length - distance
            ? distance : length - distance;
    return cyclic_distance > reset_guard;
}

static int measure_chirp(lora_fm_demod_t const *demod, uint64_t offset_q16,
        uint64_t period_q16, int32_t span, unsigned oversampling,
        int downchirp, unsigned received_reset,
        chirp_measurement_t *measurement)
{
    size_t const offset = q16_index(offset_q16);
    unsigned const length = (unsigned)q16_index(period_q16);
    unsigned const guard = oversampling * 2 > 4 ? oversampling * 2 : 4;
    unsigned const edge_guard = oversampling * LORA_FM_EDGE_GUARD_CHIPS > 4
            ? oversampling * LORA_FM_EDGE_GUARD_CHIPS : 4;
    unsigned const reset_guard = oversampling * LORA_FM_RESET_GUARD_CHIPS > 4
            ? oversampling * LORA_FM_RESET_GUARD_CHIPS : 4;
    if (!length || offset > demod->sample_count
            || length > demod->sample_count - offset
            || demod->reference_period != length
            || demod->reference_span != span) {
        return 0;
    }
    int const reset_known = received_reset < length;
    if (!reset_known) {
        received_reset = find_frequency_reset(demod, offset, length, guard,
                downchirp, span);
    }

    /* Known analyzer resets permit a bounded-cost alignment fit. A full
       measurement is retained when the reset still needs to be located. */
    unsigned histogram[LORA_FM_HISTOGRAM_BINS] = {0};
    unsigned const measurement_step = reset_known
            && length / LORA_FM_ALIGNMENT_SAMPLES
            ? length / LORA_FM_ALIGNMENT_SAMPLES : 1;
    unsigned const score_samples = (length + measurement_step - 1)
            / measurement_step;
    for (unsigned i = 0; i < length; i += measurement_step) {
        if (chirp_sample_valid(i, length, edge_guard, received_reset,
                    reset_guard)) {
            int32_t const reference = downchirp
                    ? -demod->reference[i] : demod->reference[i];
            int32_t const residual = fold_level(
                    (int32_t)demod->samples[offset + i] - reference, span);
            unsigned bin = (unsigned)(residual + span / 2)
                    * oversampling >> 10;
            if (bin >= LORA_FM_HISTOGRAM_BINS) {
                bin = LORA_FM_HISTOGRAM_BINS - 1;
            }
            histogram[bin] += 1;
        }
    }

    unsigned best_bin = 0;
    unsigned best_count = 0;
    for (unsigned bin = 0; bin < LORA_FM_HISTOGRAM_BINS; ++bin) {
        unsigned const count = histogram[(bin + LORA_FM_HISTOGRAM_BINS - 1)
                        % LORA_FM_HISTOGRAM_BINS]
                + histogram[bin]
                + histogram[(bin + 1) % LORA_FM_HISTOGRAM_BINS];
        if (count > best_count) {
            best_count = count;
            best_bin = bin;
        }
    }
    if (best_count < score_samples / 4) {
        return 0;
    }

    int32_t center = (int32_t)(((2U * best_bin + 1U) * (uint32_t)span)
            / (2U * LORA_FM_HISTOGRAM_BINS)) - span / 2;
    int32_t sum = 0;
    uint32_t error = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < length; i += measurement_step) {
        if (chirp_sample_valid(i, length, edge_guard, received_reset,
                    reset_guard)) {
            int32_t const reference = downchirp
                    ? -demod->reference[i] : demod->reference[i];
            int32_t const residual = fold_level(
                    (int32_t)demod->samples[offset + i] - reference, span);
            int32_t const delta = fold_level(residual - center, span);
            if (abs_i32(delta) <= (uint32_t)span / 8) {
                sum += delta;
                error += abs_i32(delta);
                count += 1;
            }
        }
    }
    if (count < score_samples / 4) {
        return 0;
    }
    center = fold_level(center + (int32_t)(sum / count), span);
    measurement->center = center;
    measurement->error = (uint32_t)(error / count);
    measurement->count = count;
    return 1;
}

static int prepare_preamble_template(lora_fm_demod_t *demod,
        uint64_t base_q16, uint64_t period_q16, unsigned oversampling,
        unsigned chirp_count)
{
    unsigned const period = (unsigned)q16_index(period_q16);
    if (!chirp_count) {
        return 0;
    }
    if (period > demod->template_capacity) {
        int16_t *samples = realloc(demod->preamble_template, period * sizeof(*demod->preamble_template));
        if (!samples) {
            return 0;
        }
        demod->preamble_template = samples;
        demod->template_capacity = period;
    }
    if (period > demod->shift_capacity) {
        uint16_t *shifts = realloc(demod->symbol_shifts, period * sizeof(*demod->symbol_shifts));
        if (!shifts) {
            return 0;
        }
        demod->symbol_shifts = shifts;
        demod->shift_capacity = period;
    }
    for (unsigned i = 0; i < period; ++i) {
        int32_t sum = 0;
        for (unsigned chirp = 0; chirp < chirp_count; ++chirp) {
            size_t const offset = q16_index(base_q16
                    + (uint64_t)chirp * period_q16) + i;
            if (offset >= demod->sample_count) {
                return 0;
            }
            sum += demod->samples[offset];
        }
        demod->preamble_template[i] = (int16_t)(sum / (int32_t)chirp_count);
    }

    unsigned const guard = oversampling * LORA_FM_EDGE_GUARD_CHIPS > 4
            ? oversampling * LORA_FM_EDGE_GUARD_CHIPS : 4;
    int32_t minimum_level = INT32_MAX;
    unsigned reset = 0;
    for (unsigned i = 0; i < period; ++i) {
        if (demod->preamble_template[i] < minimum_level) {
            minimum_level = demod->preamble_template[i];
            reset = i;
        }
    }
    unsigned const first = (reset + guard) & (period - 1);
    unsigned const usable = period - 2 * guard;
    /* The ideal discriminator ramp is monotonic between resets. Taking its
       cumulative maximum suppresses local noise reversals and makes a cheap
       inverse lookup possible during every payload symbol. */
    int16_t previous_level = demod->preamble_template[first];
    for (unsigned i = 1; i < usable; ++i) {
        unsigned const position = (first + i) & (period - 1);
        if (demod->preamble_template[position] < previous_level) {
            demod->preamble_template[position] = previous_level;
        }
        else {
            previous_level = demod->preamble_template[position];
        }
    }
    int32_t const minimum = demod->preamble_template[first];
    unsigned const final_position = (first + usable - 1) & (period - 1);
    int32_t const maximum = demod->preamble_template[final_position];
    if (maximum <= minimum) {
        return 0;
    }
    unsigned const inverse_size = (unsigned)(maximum - minimum + 1);
    if (inverse_size > demod->inverse_capacity) {
        uint16_t *inverse = realloc(demod->inverse_template, inverse_size * sizeof(*demod->inverse_template));
        if (!inverse) {
            return 0;
        }
        demod->inverse_template = inverse;
        demod->inverse_capacity = inverse_size;
    }
    unsigned position = first;
    for (unsigned i = 0; i < inverse_size; ++i) {
        int32_t const level = minimum + (int32_t)i;
        unsigned traversed = (position - first) & (period - 1);
        while (traversed + 1 < usable
                && abs_i32(demod->preamble_template[(position + 1)
                            & (period - 1)] - level)
                    <= abs_i32(demod->preamble_template[position] - level)) {
            position = (position + 1) & (period - 1);
            traversed += 1;
        }
        demod->inverse_template[i] = (uint16_t)position;
    }
    demod->inverse_minimum = minimum;
    demod->inverse_size = inverse_size;
    return 1;
}

static int match_chirp_symbol(lora_fm_demod_t *demod,
        uint64_t offset_q16, int32_t *matched_q8)
{
    size_t const offset = q16_index(offset_q16);
    unsigned const period = (unsigned)q16_index(demod->period_q16);
    unsigned const guard = demod->oversampling * 2 > 4
            ? demod->oversampling * 2 : 4;
    unsigned const edge_guard = demod->oversampling
            * LORA_FM_EDGE_GUARD_CHIPS > 4
            ? demod->oversampling * LORA_FM_EDGE_GUARD_CHIPS : 4;
    unsigned const reset_guard = demod->oversampling
            * LORA_FM_RESET_GUARD_CHIPS > 4
            ? demod->oversampling * LORA_FM_RESET_GUARD_CHIPS : 4;
    if (offset > demod->sample_count
            || period > demod->sample_count - offset) {
        return 0;
    }
    unsigned const reset = find_frequency_reset(demod, offset, period,
            guard, 0, demod->span);
    memset(demod->histogram, 0,
            demod->chips * sizeof(*demod->histogram));
    unsigned shift_count = 0;
    for (unsigned i = edge_guard; i + edge_guard < period; ++i) {
        if (!chirp_sample_valid(i, period, edge_guard, reset, reset_guard)) {
            continue;
        }
        int32_t const inverse_index = (int32_t)demod->samples[offset + i]
                - demod->inverse_minimum;
        if (inverse_index < 0
                || (unsigned)inverse_index >= demod->inverse_size) {
            continue;
        }
        unsigned const position = demod->inverse_template[inverse_index];
        unsigned const shift = (position - i) & (period - 1);
        demod->symbol_shifts[shift_count++] = (uint16_t)shift;
        unsigned const symbol = ((shift + demod->oversampling / 2)
                / demod->oversampling) & (demod->chips - 1);
        if (demod->histogram[symbol] != UINT16_MAX) {
            demod->histogram[symbol] += 1;
        }
    }
    unsigned best_symbol = 0;
    unsigned best_count = 0;
    for (unsigned symbol = 0; symbol < demod->chips; ++symbol) {
        unsigned const candidate_count = demod->histogram[(symbol - 1)
                        & (demod->chips - 1)]
                + demod->histogram[symbol]
                + demod->histogram[(symbol + 1) & (demod->chips - 1)];
        if (candidate_count > best_count) {
            best_count = candidate_count;
            best_symbol = symbol;
        }
    }
    if (best_count < period / 8) {
        return 0;
    }

    unsigned const expected = best_symbol * demod->oversampling;
    int32_t delta_sum = 0;
    unsigned count = 0;
    for (unsigned i = 0; i < shift_count; ++i) {
        int32_t delta = (int32_t)((demod->symbol_shifts[i] - expected)
                & (period - 1));
        if (delta >= (int32_t)period / 2) {
            delta -= period;
        }
        if (abs_i32(delta) <= 16U * demod->oversampling) {
            delta_sum += delta;
            count += 1;
        }
    }
    if (!count || count < period / 4) {
        return 0;
    }
    *matched_q8 = (int32_t)best_symbol * 256
            + (int32_t)((int64_t)delta_sum * 256
            / ((int64_t)count * demod->oversampling));
    return 1;
}

static int32_t reset_delta(lora_fm_demod_t const *demod, size_t position,
        unsigned guard)
{
    return (int32_t)demod->samples[position + guard]
            - demod->samples[position - guard];
}

static int find_reset_near(lora_fm_demod_t const *demod, size_t target,
        unsigned tolerance, unsigned guard, int32_t span, size_t *reset)
{
    if (target < tolerance + guard
            || target + tolerance + guard >= demod->sample_count) {
        return 0;
    }
    size_t best = target;
    int32_t best_delta = 0;
    for (size_t i = target - tolerance; i <= target + tolerance; ++i) {
        int32_t const delta = reset_delta(demod, i, guard);
        if (delta < best_delta) {
            best_delta = delta;
            best = i;
        }
    }
    if (best_delta >= -(2 * span) / 3) {
        return 0;
    }
    *reset = best;
    return 1;
}

static unsigned circular_bin_distance(unsigned a, unsigned b, unsigned chips)
{
    unsigned const forward = (a - b) & (chips - 1);
    unsigned const backward = (b - a) & (chips - 1);
    return forward < backward ? forward : backward;
}

static unsigned q8_symbol_bin(int32_t value, unsigned chips)
{
    return ((uint32_t)(value + (int32_t)chips * 256 * 4 + 128) >> 8)
            & (chips - 1);
}

/** Locate and validate the two network-ID chirps after the preamble. */
static int detect_sync_word(lora_fm_demod_t *demod,
        uint64_t base_q16, uint64_t period_q16, unsigned requested_sync_word,
        unsigned *sync_index, unsigned *detected_sync_word)
{
    int32_t preamble_q8;
    if (!match_chirp_symbol(demod, base_q16, &preamble_q8)) {
        return 0;
    }
    unsigned const preamble_bin = q8_symbol_bin(preamble_q8, demod->chips);

    for (unsigned i = LORA_FM_MIN_PREAMBLE_RESETS;
            i < LORA_FM_SEARCH_SYMBOLS; ++i) {
        int32_t high_q8;
        if (!match_chirp_symbol(demod, base_q16 + i * period_q16,
                    &high_q8)) {
            return 0;
        }
        unsigned const high_bin = q8_symbol_bin(high_q8, demod->chips);
        if (circular_bin_distance(high_bin, preamble_bin,
                    demod->chips) <= 4) {
            continue;
        }

        int32_t low_q8;
        if (!match_chirp_symbol(demod,
                    base_q16 + (i + 1) * period_q16, &low_q8)) {
            return 0;
        }
        unsigned const low_bin = q8_symbol_bin(low_q8, demod->chips);
        unsigned const high_delta = (high_bin - preamble_bin)
                & (demod->chips - 1);
        unsigned const low_delta = (low_bin - preamble_bin)
                & (demod->chips - 1);
        unsigned const high_nibble = ((high_delta + 4) / 8) & 0x0f;
        unsigned const low_nibble = ((low_delta + 4) / 8) & 0x0f;
        unsigned const word = high_nibble << 4 | low_nibble;
        if (circular_bin_distance(high_delta, high_nibble * 8,
                    demod->chips) <= 4
                && circular_bin_distance(low_delta, low_nibble * 8,
                    demod->chips) <= 4
                && word && (!requested_sync_word
                    || word == requested_sync_word)) {
            *sync_index = i;
            *detected_sync_word = word;
            return 1;
        }
    }
    return 0;
}

/** Fit residual bin phase and drift across all available preamble chirps. */
static int estimate_preamble_clock(lora_fm_demod_t *demod,
        uint64_t base_q16, uint64_t period_q16, unsigned preamble_count,
        int32_t *phase_q8, int32_t *drift_q8)
{
    if (preamble_count < LORA_FM_MIN_PREAMBLE_RESETS) {
        return 0;
    }
    int32_t const symbol_period_q8 = (int32_t)demod->chips * 256;
    int32_t const half_period_q8 = symbol_period_q8 / 2;
    int32_t previous = 0;
    int32_t sum_x = 0;
    int32_t sum_y = 0;
    int32_t sum_xx = 0;
    int32_t sum_xy = 0;
    for (unsigned i = 0; i < preamble_count; ++i) {
        int32_t value;
        if (!match_chirp_symbol(demod, base_q16 + i * period_q16,
                    &value)) {
            return 0;
        }
        value %= symbol_period_q8;
        if (value >= half_period_q8) {
            value -= symbol_period_q8;
        }
        if (i) {
            while (value - previous > half_period_q8) {
                value -= symbol_period_q8;
            }
            while (previous - value > half_period_q8) {
                value += symbol_period_q8;
            }
        }
        previous = value;
        sum_x += (int32_t)i;
        sum_y += value;
        sum_xx += (int32_t)(i * i);
        sum_xy += (int32_t)i * value;
    }

    int32_t const count = (int32_t)preamble_count;
    int32_t const denominator = count * sum_xx - sum_x * sum_x;
    if (!denominator) {
        return 0;
    }
    int32_t const slope = (count * sum_xy - sum_x * sum_y)
            / denominator;
    int32_t const intercept = (sum_y - slope * sum_x) / count;
    int32_t const data_index_q2 = (int32_t)(4 * preamble_count + 17);
    int32_t data_phase = intercept + slope * data_index_q2 / 4;
    data_phase %= symbol_period_q8;
    if (data_phase >= half_period_q8) {
        data_phase -= symbol_period_q8;
    }
    else if (data_phase < -half_period_q8) {
        data_phase += symbol_period_q8;
    }
    *phase_q8 = -data_phase;
    *drift_q8 = -slope;
    return 1;
}

/** Estimate clock error directly from the discriminator ramp levels.

    This is less precise than fitting matched preamble bins when the inverse
    template is clean, but it is independent of that inversion and therefore
    provides a useful second hypothesis for distorted captures.
*/
static int estimate_level_clock(lora_fm_demod_t *demod,
        uint64_t base_q16, uint64_t period_q16, int32_t span,
        unsigned oversampling, unsigned chips, int32_t *phase_q8,
        int32_t *drift_q8)
{
    chirp_measurement_t measured[LORA_FM_MIN_PREAMBLE_RESETS];
    for (unsigned i = 0; i < LORA_FM_MIN_PREAMBLE_RESETS; ++i) {
        if (!measure_chirp(demod, base_q16 + i * period_q16,
                    period_q16, span, oversampling, 0, UINT_MAX,
                    &measured[i])) {
            return 0;
        }
    }

    int32_t preamble_q8;
    if (!match_chirp_symbol(demod, base_q16, &preamble_q8)) {
        return 0;
    }
    int32_t const symbol_period_q8 = (int32_t)chips * 256;
    preamble_q8 %= symbol_period_q8;
    if (preamble_q8 >= symbol_period_q8 / 2) {
        preamble_q8 -= symbol_period_q8;
    }
    else if (preamble_q8 < -symbol_period_q8 / 2) {
        preamble_q8 += symbol_period_q8;
    }

    int32_t const level_delta = fold_level(
            measured[LORA_FM_MIN_PREAMBLE_RESETS - 1].center
                - measured[0].center, span);
    int32_t const level_drift = level_delta
            / (int32_t)(LORA_FM_MIN_PREAMBLE_RESETS - 1);
    int32_t const symbol_drift = -(int32_t)((int64_t)level_drift
            * chips * 256 / span);
    *phase_q8 = -preamble_q8;
    *drift_q8 = symbol_drift / 3;
    return 1;
}

static int score_reference_alignment(lora_fm_demod_t const *demod,
        size_t const resets[LORA_FM_MIN_PREAMBLE_RESETS], int adjustment,
        uint64_t period_q16, int32_t span, unsigned oversampling,
        uint64_t *base_q16, uint32_t *score)
{
    if (adjustment < 0 && (size_t)-adjustment > resets[0]) {
        return 0;
    }
    size_t const base = adjustment < 0
            ? resets[0] - (size_t)-adjustment
            : resets[0] + (size_t)adjustment;
    *base_q16 = (uint64_t)base << 16;
    unsigned const period = (unsigned)q16_index(period_q16);
    chirp_measurement_t measured[LORA_FM_MIN_PREAMBLE_RESETS];
    for (unsigned i = 0; i < LORA_FM_MIN_PREAMBLE_RESETS; ++i) {
        size_t const offset = base + i * (size_t)period;
        unsigned const received_reset = (unsigned)(resets[i] - offset)
                & (period - 1);
        if (!measure_chirp(demod, *base_q16 + i * period_q16,
                    period_q16, span, oversampling, 0, received_reset,
                    &measured[i])) {
            return 0;
        }
    }
    int32_t const center = measured[0].center;
    *score = 0;
    for (unsigned i = 0; i < LORA_FM_MIN_PREAMBLE_RESETS; ++i) {
        int32_t const delta = fold_level(measured[i].center - center, span);
        *score += measured[i].error + abs_i32(delta);
    }
    return 1;
}

static int lock_frame(lora_fm_demod_t *demod,
        size_t const resets[LORA_FM_MIN_PREAMBLE_RESETS], unsigned sf,
        uint32_t bandwidth, unsigned oversampling, unsigned period,
        int32_t span, unsigned requested_sync_word)
{
    /* Reset minima move within the low-pass transient. They validate the
       repetition, but the exact integer analyzer clock is more accurate. */
    uint64_t const period_q16 = (uint64_t)period << 16;
    unsigned const guard = oversampling * 2 > 4 ? oversampling * 2 : 4;
    uint32_t best_score = UINT32_MAX;
    uint64_t best_base_q16 = 0;
    int have_best = 0;
    unsigned const measured_period = (unsigned)q16_index(period_q16);
    if (!prepare_reference(demod, measured_period, span)) {
        return 0;
    }

    /* Cyclic chirps have a broad timing ambiguity. Half-guard spacing covers
       the complete useful phase range without a sample-by-sample sweep. */
    for (int adjustment = -(int)(4 * guard);
            adjustment <= (int)(4 * guard); adjustment += (int)guard / 2) {
        uint64_t base_q16;
        uint32_t score;
        if (!score_reference_alignment(demod, resets, adjustment,
                    period_q16, span, oversampling, &base_q16, &score)) {
            continue;
        }
        if (score < best_score) {
            best_score = score;
            best_base_q16 = base_q16;
            have_best = 1;
        }
    }
    unsigned const chips = 1U << sf;
    if (!have_best || best_score
            > LORA_FM_MIN_PREAMBLE_RESETS * (uint32_t)span / 8) {
        return 0;
    }
    demod->chips = chips;
    demod->oversampling = oversampling;
    demod->span = span;
    demod->period_q16 = period_q16;
    if (!prepare_preamble_template(demod, best_base_q16, period_q16,
                oversampling, LORA_FM_MIN_PREAMBLE_RESETS)) {
        return 0;
    }

    unsigned sync_index;
    unsigned detected_sync_word;
    if (!detect_sync_word(demod, best_base_q16, period_q16,
                requested_sync_word, &sync_index, &detected_sync_word)) {
        return 0;
    }
    /* Keep the template local to the first four chirps. Averaging a longer
       preamble without first correcting its sample-clock drift blurs the
       filter transient that provides the inverse template's timing. */
    if (!estimate_preamble_clock(demod, best_base_q16, period_q16,
                sync_index, &demod->symbol_phase_q8[0],
                &demod->symbol_drift_q8[0])) {
        return 0;
    }
    demod->clock_count = 1;
    if (estimate_level_clock(demod, best_base_q16, period_q16, span,
                oversampling, chips, &demod->symbol_phase_q8[1],
                &demod->symbol_drift_q8[1])) {
        demod->clock_count = 2;
    }

    /* The sync pair fixes the SFD location. As in the FFT receiver, the PHY
       header and payload CRC are stronger validation than a strict fit of the
       two filter-distorted downchirps. */
    demod->locked = 1;
    demod->spreading_factor = sf;
    demod->bandwidth = bandwidth;
    demod->sync_word = detected_sync_word;
    demod->chips = chips;
    demod->oversampling = oversampling;
    demod->span = span;
    demod->period_q16 = period_q16;
    uint64_t const preamble_lead_q16 = sync_index <= 8
            ? (uint64_t)(8 - sync_index) * period_q16 : 0;
    demod->frame_start_q16 = preamble_lead_q16 <= best_base_q16
            ? best_base_q16 - preamble_lead_q16 : 0;
    demod->data_offset_q16 = best_base_q16
            + (uint64_t)(4 * sync_index + 17) * period_q16 / 4;
    demod->matched_symbol_count = 0;
    demod->correction_count = 0;
    demod->corrections_ready = 0;
    return 1;
}

static void corrected_symbols(lora_fm_demod_t const *demod,
        correction_candidate_t const *correction, unsigned count,
        uint16_t *symbols)
{
    int32_t const phase_q8 = demod->symbol_phase_q8[correction->clock_index]
            + correction->phase_adjust_q8;
    int32_t const drift_q8 = demod->symbol_drift_q8[correction->clock_index]
            + correction->drift_adjust_q8;
    for (unsigned i = 0; i < count; ++i) {
        int32_t const value = demod->matched_q8[i] + phase_q8
                + (int32_t)i * drift_q8
                + (int32_t)demod->chips * 256 * 4 + 128;
        symbols[i] = (uint16_t)(((uint32_t)value >> 8)
                & (demod->chips - 1));
    }
}

static uint32_t correction_fit_error(lora_fm_demod_t const *demod,
        correction_candidate_t const *correction, unsigned count)
{
    int32_t const phase_q8 = demod->symbol_phase_q8[correction->clock_index]
            + correction->phase_adjust_q8;
    int32_t const drift_q8 = demod->symbol_drift_q8[correction->clock_index]
            + correction->drift_adjust_q8;
    uint32_t error = 0;
    for (unsigned i = 0; i < count; ++i) {
        int32_t residual = (demod->matched_q8[i] + phase_q8
                + (int32_t)i * drift_q8) & 0xff;
        if (residual >= 128) {
            residual -= 256;
        }
        error += abs_i32(residual);
    }
    return error;
}

/** Match only symbols not already processed by a previous input block. */
static int match_symbols_until(lora_fm_demod_t *demod, unsigned target)
{
    for (unsigned i = demod->matched_symbol_count; i < target; ++i) {
        if (!match_chirp_symbol(demod, demod->data_offset_q16
                    + (uint64_t)i * demod->period_q16,
                    &demod->matched_q8[i])) {
            return 0;
        }
        demod->matched_symbol_count = i + 1;
    }
    return 1;
}

/** Use the protected explicit header to cache viable phase/drift combinations.

    Retaining each candidate's declared length avoids retrying the Cartesian
    correction sweep whenever another input block extends an incomplete
    payload. Invalid headers are discarded before payload matching.
*/
static int prepare_corrections(lora_fm_demod_t *demod)
{
    uint16_t symbols[8];
    demod->correction_count = 0;
    for (unsigned clock = 0; clock < demod->clock_count; ++clock) {
        for (unsigned drift_attempt = 0; drift_attempt
                < sizeof(drift_adjustments) / sizeof(*drift_adjustments);
                ++drift_attempt) {
            for (unsigned phase_attempt = 0; phase_attempt
                    < sizeof(phase_eighths) / sizeof(*phase_eighths);
                    ++phase_attempt) {
                correction_candidate_t correction = {
                        .phase_adjust_q8 = (int16_t)(
                            phase_eighths[phase_attempt] * 32),
                        .drift_adjust_q8 = (int16_t)
                            drift_adjustments[drift_attempt],
                        .clock_index = (uint8_t)clock,
                };
                corrected_symbols(demod, &correction, 8, symbols);
                lora_packet_t ignored = {0};
                unsigned expected = 0;
                lora_symbols_result_t const result = lora_decode_symbols_ex(
                        symbols, 8, demod->spreading_factor,
                        demod->bandwidth, &ignored, &expected);
                if (result == LORA_SYMBOLS_INVALID || expected < 8
                        || expected > LORA_FM_MAX_SYMBOLS) {
                    continue;
                }
                if (demod->correction_count < LORA_FM_MAX_CORRECTIONS) {
                    correction.expected_symbols = (uint16_t)expected;
                    demod->corrections[demod->correction_count++] =
                            correction;
                }
            }
        }
    }
    demod->corrections_ready = 1;
    return demod->correction_count != 0;
}

static unsigned minimum_expected_symbols(lora_fm_demod_t const *demod)
{
    unsigned minimum = LORA_FM_MAX_SYMBOLS + 1;
    for (unsigned i = 0; i < demod->correction_count; ++i) {
        if (demod->corrections[i].expected_symbols < minimum) {
            minimum = demod->corrections[i].expected_symbols;
        }
    }
    return minimum;
}

static int try_decode_locked(lora_fm_demod_t *demod, lora_packet_t *packet,
        size_t *consumed)
{
    size_t const data_offset = q16_index(demod->data_offset_q16);
    if (data_offset >= demod->sample_count) {
        return 0;
    }
    uint64_t const available_q16 = (uint64_t)(demod->sample_count
            - data_offset) << 16;
    unsigned available_symbols = (unsigned)(available_q16 / demod->period_q16);
    if (available_symbols > LORA_FM_MAX_SYMBOLS) {
        available_symbols = LORA_FM_MAX_SYMBOLS;
    }
    if (available_symbols < 8) {
        return 0;
    }

    /* Decode the eight-symbol header first; it bounds all subsequent work. */
    if (!match_symbols_until(demod, 8)
            || (!demod->corrections_ready && !prepare_corrections(demod))) {
        *consumed = q16_index(demod->data_offset_q16
                + (uint64_t)demod->matched_symbol_count
                    * demod->period_q16);
        return -1;
    }

    uint16_t adjusted[LORA_FM_MAX_SYMBOLS];
    while (demod->correction_count) {
        unsigned const target = minimum_expected_symbols(demod);
        if (target > available_symbols) {
            return 0;
        }
        if (!match_symbols_until(demod, target)) {
            *consumed = q16_index(demod->data_offset_q16
                    + (uint64_t)demod->matched_symbol_count
                        * demod->period_q16);
            return -1;
        }

        correction_candidate_t repair[LORA_FM_REPAIR_CANDIDATES];
        uint32_t repair_score[LORA_FM_REPAIR_CANDIDATES];
        for (unsigned i = 0; i < LORA_FM_REPAIR_CANDIDATES; ++i) {
            repair_score[i] = UINT32_MAX;
        }
        unsigned remaining = 0;
        for (unsigned i = 0; i < demod->correction_count; ++i) {
            correction_candidate_t correction = demod->corrections[i];
            if (demod->matched_symbol_count < correction.expected_symbols) {
                demod->corrections[remaining++] = correction;
                continue;
            }
            corrected_symbols(demod, &correction,
                    demod->matched_symbol_count, adjusted);
            if (correction.expected_symbols == demod->matched_symbol_count) {
                uint32_t const score = correction_fit_error(demod,
                        &correction, demod->matched_symbol_count);
                for (unsigned rank = 0; rank < LORA_FM_REPAIR_CANDIDATES;
                        ++rank) {
                    if (score < repair_score[rank]) {
                        for (unsigned move = LORA_FM_REPAIR_CANDIDATES - 1;
                                move > rank; --move) {
                            repair_score[move] = repair_score[move - 1];
                            repair[move] = repair[move - 1];
                        }
                        repair_score[rank] = score;
                        repair[rank] = correction;
                        break;
                    }
                }
            }
            unsigned expected = 0;
            lora_symbols_result_t const result = lora_decode_symbols_ex(
                    adjusted, demod->matched_symbol_count,
                    demod->spreading_factor, demod->bandwidth, packet,
                    &expected);
            if (result == LORA_SYMBOLS_VALID) {
                packet->bandwidth = demod->bandwidth;
                packet->sync_word = demod->sync_word;
                packet->start_offset = demod->sample_offset
                        + q16_index(demod->frame_start_q16);
                packet->end_offset = demod->sample_offset
                        + q16_index(demod->data_offset_q16
                            + (uint64_t)expected * demod->period_q16);
                *consumed = q16_index(demod->data_offset_q16
                        + (uint64_t)expected * demod->period_q16);
                return 1;
            }
            if (result == LORA_SYMBOLS_INCOMPLETE
                    && expected <= LORA_FM_MAX_SYMBOLS) {
                correction.expected_symbols = (uint16_t)expected;
                demod->corrections[remaining++] = correction;
            }
        }
        for (unsigned i = 0; i < LORA_FM_REPAIR_CANDIDATES
                && repair_score[i] != UINT32_MAX; ++i) {
            corrected_symbols(demod, &repair[i],
                    demod->matched_symbol_count, adjusted);
            if (lora_decode_symbols_repaired(adjusted,
                        demod->matched_symbol_count,
                        demod->spreading_factor, demod->bandwidth, packet,
                        LORA_FM_REPAIR_TRIALS)) {
                packet->bandwidth = demod->bandwidth;
                packet->sync_word = demod->sync_word;
                packet->start_offset = demod->sample_offset
                        + q16_index(demod->frame_start_q16);
                packet->end_offset = demod->sample_offset
                        + q16_index(demod->data_offset_q16
                            + (uint64_t)demod->matched_symbol_count
                                * demod->period_q16);
                *consumed = q16_index(demod->data_offset_q16
                        + (uint64_t)demod->matched_symbol_count
                            * demod->period_q16);
                return 1;
            }
        }
        demod->correction_count = remaining;
    }
    *consumed = q16_index(demod->data_offset_q16
            + (uint64_t)demod->matched_symbol_count * demod->period_q16);
    return -1;
}

/** Cheaply reject periodic reset coincidences that are not rising chirps. */
static int preamble_repeats(lora_fm_demod_t const *demod, size_t offset,
        unsigned period, unsigned guard, int32_t span)
{
    unsigned const step = period / 32 ? period / 32 : 1;
    uint32_t error = 0;
    unsigned count = 0;
    for (unsigned position = guard; position + guard < period;
            position += step) {
        int32_t const reference = demod->samples[offset + position];
        for (unsigned chirp = 1; chirp < LORA_FM_MIN_PREAMBLE_RESETS;
                ++chirp) {
            int32_t const delta = fold_level(
                    demod->samples[offset + chirp * (size_t)period + position]
                        - reference,
                    span);
            error += abs_i32(delta);
            count += 1;
        }
    }
    if (!count || error / count > (uint32_t)span / 8) {
        return 0;
    }

    /* Repetition also admits periodic FSK and noise. Away from the reset, an
       upchirp must rise by span/16 over one sixteenth of a symbol. */
    unsigned const slope_step = period / 16;
    int32_t const expected_slope = span / 16;
    error = 0;
    count = 0;
    for (unsigned chirp = 0; chirp < LORA_FM_MIN_PREAMBLE_RESETS; ++chirp) {
        size_t const base = offset + chirp * (size_t)period;
        for (unsigned position = guard;
                position + slope_step + guard < period;
                position += slope_step) {
            int32_t const slope = fold_level(
                    demod->samples[base + position + slope_step]
                        - demod->samples[base + position],
                    span);
            error += abs_i32(fold_level(slope - expected_slope, span));
            count += 1;
        }
    }
    return count && error / count <= (uint32_t)span / 8;
}

static void clear_reset_cache(reset_cache_t *cache)
{
    cache->count = 0;
    memset(cache->tested, 0, sizeof(cache->tested));
    cache->scan_offset = 0;
    cache->bandwidth = 0;
    cache->guard = 0;
    cache->span = 0;
}

static void clear_reset_search(lora_fm_demod_t *demod)
{
    for (unsigned i = 0; i < LORA_FM_RESET_CACHE_COUNT; ++i) {
        clear_reset_cache(&demod->reset_cache[i]);
    }
    demod->search_sync_valid = 0;
}

static void rewind_reset_candidates(lora_fm_demod_t *demod)
{
    for (unsigned i = 0; i < LORA_FM_RESET_CACHE_COUNT; ++i) {
        memset(demod->reset_cache[i].tested, 0,
                sizeof(demod->reset_cache[i].tested));
    }
}

static reset_cache_t *reset_cache_for(lora_fm_demod_t *demod,
        uint32_t bandwidth, unsigned guard, int32_t span)
{
    reset_cache_t *available = NULL;
    for (unsigned i = 0; i < LORA_FM_RESET_CACHE_COUNT; ++i) {
        reset_cache_t *cache = &demod->reset_cache[i];
        if (cache->bandwidth == bandwidth && cache->guard == guard
                && cache->span == span) {
            return cache;
        }
        if (!cache->bandwidth && !available) {
            available = cache;
        }
    }
    reset_cache_t *cache = available ? available : &demod->reset_cache[0];
    clear_reset_cache(cache);
    cache->bandwidth = bandwidth;
    cache->guard = guard;
    cache->span = span;
    if (demod->sample_offset <= UINT64_MAX - 2U * guard) {
        cache->scan_offset = demod->sample_offset + 2U * guard;
    }
    return cache;
}

static int append_reset_candidate(reset_cache_t *cache, uint64_t position)
{
    if (cache->count && cache->positions[cache->count - 1] == position) {
        return 1;
    }
    if (cache->count == cache->capacity) {
        if (cache->capacity > SIZE_MAX / 2 / sizeof(*cache->positions)) {
            return 0;
        }
        size_t const capacity = cache->capacity
                ? 2 * cache->capacity : 256;
        uint64_t *positions = realloc(cache->positions, capacity * sizeof(*positions));
        if (!positions) {
            return 0;
        }
        cache->positions = positions;
        cache->capacity = capacity;
    }
    cache->positions[cache->count++] = position;
    return 1;
}

/** Extend one bandwidth's reset list through all currently complete probes. */
static int extend_reset_cache(lora_fm_demod_t const *demod,
        reset_cache_t *cache)
{
    if (demod->sample_count > UINT64_MAX - demod->sample_offset) {
        return 0;
    }
    uint64_t const start = demod->sample_offset;
    uint64_t const end = start + demod->sample_count;
    uint64_t const minimum = start + 2U * cache->guard;
    uint64_t probe = cache->scan_offset < minimum
            ? minimum : cache->scan_offset;

    /* reset_delta() spans 2 * guard samples. A guard-spaced probe grid
       intersects every ideal reset, then the local sweep restores timing. */
    while (probe < end && cache->guard < end - probe) {
        size_t const local = (size_t)(probe - start);
        int32_t const delta = reset_delta(demod, local, cache->guard);
        if (delta >= -(2 * cache->span) / 3) {
            probe += cache->guard;
            continue;
        }
        size_t best = local;
        int32_t best_delta = delta;
        for (size_t i = local - cache->guard;
                i <= local + cache->guard; ++i) {
            int32_t const candidate_delta = reset_delta(demod, i,
                    cache->guard);
            if (candidate_delta < best_delta) {
                best_delta = candidate_delta;
                best = i;
            }
        }
        uint64_t const position = start + best;
        if (!append_reset_candidate(cache, position)) {
            return 0;
        }
        probe = position + 2U * cache->guard;
    }
    cache->scan_offset = probe;
    return 1;
}

static void prune_reset_caches(lora_fm_demod_t *demod)
{
    for (unsigned i = 0; i < LORA_FM_RESET_CACHE_COUNT; ++i) {
        reset_cache_t *cache = &demod->reset_cache[i];
        if (!cache->bandwidth) {
            continue;
        }
        uint64_t const cutoff = demod->sample_offset + 2U * cache->guard;
        size_t removed = 0;
        while (removed < cache->count
                && cache->positions[removed] < cutoff) {
            removed += 1;
        }
        if (removed) {
            memmove(cache->positions, cache->positions + removed,
                    (cache->count - removed) * sizeof(*cache->positions));
            cache->count -= removed;
            for (unsigned sf = 0; sf < LORA_FM_SF_COUNT; ++sf) {
                cache->tested[sf] = cache->tested[sf] > removed
                        ? cache->tested[sf] - removed : 0;
            }
        }
        if (cache->scan_offset < cutoff) {
            cache->scan_offset = cutoff;
        }
    }
}

static int search_candidate(lora_fm_demod_t *demod, unsigned sf,
        uint32_t bandwidth, unsigned requested_sync_word)
{
    unsigned oversampling;
    unsigned period;
    int32_t span;
    if (!configure_candidate(demod->sample_rate, sf, bandwidth,
                &oversampling, &period, &span)) {
        return 0;
    }
    unsigned const guard = oversampling * 2 > 4 ? oversampling * 2 : 4;
    unsigned const tolerance = 2 * guard;
    size_t const required = LORA_FM_SEARCH_SYMBOLS
            * (size_t)period + tolerance + guard;
    if (demod->sample_count <= required) {
        return 0;
    }
    reset_cache_t *cache = reset_cache_for(demod, bandwidth, guard, span);
    if (!extend_reset_cache(demod, cache)) {
        return -1;
    }
    size_t *tested = &cache->tested[sf - LORA_FM_MIN_SF];
    while (*tested < cache->count) {
        uint64_t const absolute = cache->positions[*tested];
        if (absolute < demod->sample_offset + 2U * guard) {
            *tested += 1;
            continue;
        }
        size_t const i = (size_t)(absolute - demod->sample_offset);
        if (i >= demod->sample_count
                || required >= demod->sample_count - i) {
            break;
        }
        *tested += 1;
        size_t resets[LORA_FM_MIN_PREAMBLE_RESETS] = {i};
        int repeated = 1;
        for (unsigned k = 1; k < LORA_FM_MIN_PREAMBLE_RESETS; ++k) {
            repeated &= find_reset_near(demod, i + k * (size_t)period,
                    tolerance, guard, span, &resets[k]);
        }
        if (repeated) {
            repeated = preamble_repeats(demod, i, period, guard, span);
        }
        if (repeated && lock_frame(demod, resets, sf, bandwidth,
                    oversampling, period, span, requested_sync_word)) {
            return 1;
        }
    }
    return 0;
}

static void consume_samples(lora_fm_demod_t *demod, size_t count)
{
    if (count > demod->sample_count) {
        count = demod->sample_count;
    }
    memmove(demod->samples, demod->samples + count,
            (demod->sample_count - count) * sizeof(*demod->samples));
    demod->sample_count -= count;
    demod->sample_offset += count;
    prune_reset_caches(demod);
    demod->locked = 0;
    demod->matched_symbol_count = 0;
    demod->correction_count = 0;
    demod->corrections_ready = 0;
}

lora_fm_demod_t *lora_fm_demod_create(void)
{
    lora_fm_demod_t *demod = calloc(1, sizeof(*demod));
    if (!demod) {
        return NULL;
    }
    return demod;
}

void lora_fm_demod_free(lora_fm_demod_t *demod)
{
    if (demod) {
        for (unsigned i = 0; i < LORA_FM_RESET_CACHE_COUNT; ++i) {
            free(demod->reset_cache[i].positions);
        }
        free(demod->inverse_template);
        free(demod->symbol_shifts);
        free(demod->preamble_template);
        free(demod->reference);
        free(demod->samples);
        free(demod);
    }
}

void lora_fm_demod_reset(lora_fm_demod_t *demod)
{
    if (demod) {
        demod->sample_count = 0;
        demod->sample_offset = 0;
        demod->sample_rate = 0;
        demod->locked = 0;
        demod->matched_symbol_count = 0;
        demod->correction_count = 0;
        demod->corrections_ready = 0;
        clear_reset_search(demod);
    }
}

int lora_fm_demod_process(lora_fm_demod_t *demod, int16_t const *fm,
        size_t sample_count, uint64_t sample_offset, uint32_t sample_rate,
        unsigned spreading_factor, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets)
{
    if (!demod || !fm || (!packets && max_packets)
            || !append_samples(demod, fm, sample_count, sample_offset,
                sample_rate)) {
        return -2;
    }
    if (!demod->search_sync_valid
            || demod->search_sync_word != sync_word) {
        rewind_reset_candidates(demod);
        demod->search_sync_word = sync_word;
        demod->search_sync_valid = 1;
    }

    unsigned packet_count = 0;
    while (packet_count < max_packets) {
        if (demod->locked) {
            size_t consumed = 0;
            int const decoded = try_decode_locked(demod,
                    &packets[packet_count], &consumed);
            if (decoded > 0) {
                packet_count += 1;
                consume_samples(demod, consumed);
                continue;
            }
            if (decoded < 0 || demod->matched_symbol_count
                    >= LORA_FM_MAX_SYMBOLS) {
                consume_samples(demod, consumed ? consumed
                        : q16_index(demod->frame_start_q16
                            + demod->period_q16));
                continue;
            }
            break;
        }

        static uint32_t const bandwidths[] = {500000, 250000, 125000, 62500};
        unsigned const selected_sf = spreading_factor ? spreading_factor
                : packet_count ? demod->spreading_factor : 0;
        uint32_t const selected_bandwidth = bandwidth ? bandwidth
                : packet_count ? demod->bandwidth : 0;
        unsigned const sf_first = selected_sf ? selected_sf : LORA_FM_MIN_SF;
        unsigned const sf_last = selected_sf ? selected_sf : LORA_FM_MAX_SF;
        int found = 0;
        for (unsigned sf = sf_first; sf <= sf_last && !found; ++sf) {
            for (unsigned i = 0;
                    i < sizeof(bandwidths) / sizeof(*bandwidths); ++i) {
                uint32_t const candidate_bandwidth = selected_bandwidth
                        ? selected_bandwidth : bandwidths[i];
                int const searched = search_candidate(demod, sf,
                        candidate_bandwidth, sync_word);
                if (searched < 0) {
                    return -2;
                }
                found = searched;
                if (selected_bandwidth || found) {
                    break;
                }
            }
        }
        if (!found) {
            unsigned max_period = 0;
            for (unsigned sf = sf_first; sf <= sf_last; ++sf) {
                for (unsigned i = 0; i < sizeof(bandwidths)
                        / sizeof(*bandwidths); ++i) {
                    unsigned os;
                    unsigned period;
                    int32_t span;
                    uint32_t const candidate_bandwidth = selected_bandwidth
                            ? selected_bandwidth : bandwidths[i];
                    if (configure_candidate(sample_rate, sf,
                                candidate_bandwidth, &os, &period, &span)
                            && period > max_period) {
                        max_period = period;
                    }
                    if (selected_bandwidth) {
                        break;
                    }
                }
            }
            size_t const keep = LORA_FM_SEARCH_SYMBOLS * (size_t)max_period;
            if (keep && demod->sample_count > keep) {
                consume_samples(demod, demod->sample_count - keep);
            }
            break;
        }
    }
    return (int)packet_count;
}
