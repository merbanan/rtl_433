#include "lora_fm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 1000000U
#define BANDWIDTH 125000U
#define SPREADING_FACTOR 7U
#define OVERSAMPLING (SAMPLE_RATE / BANDWIDTH)
#define CHIPS (1U << SPREADING_FACTOR)
#define PERIOD (CHIPS * OVERSAMPLING)
#define SPAN ((2U * INT16_MAX + OVERSAMPLING / 2) / OVERSAMPLING)

typedef struct {
    int32_t previous_input;
    int32_t previous_output;
} filter_state_t;

static void append_sample(int16_t *samples, unsigned *count,
        filter_state_t *filter, int32_t input)
{
    int32_t const output = (8348 * filter->previous_output
            + 4017 * (input + filter->previous_input)) >> 14;
    samples[(*count)++] = (int16_t)output;
    filter->previous_input = input;
    filter->previous_output = output;
}

static void append_upchirp(int16_t *samples, unsigned *count,
        filter_state_t *filter, unsigned symbol)
{
    unsigned const shift = symbol * OVERSAMPLING;
    for (unsigned i = 0; i < PERIOD; ++i) {
        unsigned const phase = (i + shift) & (PERIOD - 1);
        int32_t const input = -(int32_t)SPAN / 2
                + (int32_t)((uint64_t)phase * SPAN / PERIOD);
        append_sample(samples, count, filter, input);
    }
}

static void append_downchirp(int16_t *samples, unsigned *count,
        filter_state_t *filter, unsigned length)
{
    for (unsigned i = 0; i < length; ++i) {
        int32_t const input = (int32_t)SPAN / 2
                - (int32_t)((uint64_t)i * SPAN / PERIOD);
        append_sample(samples, count, filter, input);
    }
}

int main(void)
{
    static uint16_t const symbols[] = {
            109, 9, 97, 61, 29, 109, 57, 121, 99, 106, 55, 41, 94, 119,
            82, 87, 112, 65, 35, 93, 57, 76, 14, 31, 127, 94, 59, 44,
            115, 38, 23, 50, 111,
    };
    static uint8_t const expected[] = {
            0x40, 0xda, 0x1b, 0x01, 0x26, 0x80, 0x34, 0x12,
            0x01, 0xaa, 0x55, 0x10, 0x20, 0x30, 0x40,
    };
    unsigned const capacity = 2 * PERIOD
            + (8 + 2 + 2 + sizeof(symbols) / sizeof(*symbols)) * PERIOD
            + PERIOD / 4;
    int16_t *samples = calloc(capacity, sizeof(*samples));
    if (!samples) {
        return 2;
    }
    lora_fm_demod_t *demod = lora_fm_demod_create();
    if (!demod) {
        free(samples);
        return 2;
    }

    filter_state_t filter = {0};
    unsigned count = 0;
    for (unsigned i = 0; i < PERIOD; ++i) {
        append_sample(samples, &count, &filter, 0);
    }
    for (unsigned i = 0; i < 8; ++i) {
        append_upchirp(samples, &count, &filter, 0);
    }
    append_upchirp(samples, &count, &filter, 3 * 8);
    append_upchirp(samples, &count, &filter, 4 * 8);
    append_downchirp(samples, &count, &filter, PERIOD);
    append_downchirp(samples, &count, &filter, PERIOD);
    append_downchirp(samples, &count, &filter, PERIOD / 4);
    for (unsigned i = 0; i < sizeof(symbols) / sizeof(*symbols); ++i) {
        append_upchirp(samples, &count, &filter, symbols[i]);
    }
    for (unsigned i = 0; i < PERIOD; ++i) {
        append_sample(samples, &count, &filter, 0);
    }

    lora_packet_t packet = {0};
    int const decoded = lora_fm_demod_process(demod, samples, count, 0,
            SAMPLE_RATE, SPREADING_FACTOR, BANDWIDTH, 0x34, &packet, 1);
    int const valid = decoded == 1 && packet.payload_len == sizeof(expected)
            && packet.spreading_factor == SPREADING_FACTOR
            && packet.bandwidth == BANDWIDTH && packet.sync_word == 0x34
            && !memcmp(packet.payload, expected, sizeof(expected));
    int stream_valid = 1;
    unsigned stream_packets = 0;
    lora_fm_demod_reset(demod);
    for (unsigned offset = 0; offset < count; offset += 997) {
        unsigned const remaining = count - offset;
        unsigned const block = remaining < 997 ? remaining : 997;
        lora_packet_t streamed = {0};
        int const result = lora_fm_demod_process(demod, samples + offset,
                block, offset, SAMPLE_RATE, SPREADING_FACTOR, BANDWIDTH,
                0x34, &streamed, 1);
        if (result < 0) {
            stream_valid = 0;
            break;
        }
        if (result == 1) {
            stream_packets += 1;
            stream_valid &= streamed.payload_len == sizeof(expected)
                    && !memcmp(streamed.payload, expected, sizeof(expected));
        }
    }
    stream_valid &= stream_packets == 1;
    int analyzer_valid = 1;
    unsigned analyzer_packets = 0;
    lora_fm_demod_reset(demod);
    for (unsigned offset = 0; offset < count; offset += 997) {
        unsigned const remaining = count - offset;
        unsigned const block = remaining < 997 ? remaining : 997;
        lora_packet_t analyzed = {0};
        int const result = lora_fm_demod_process(demod, samples + offset,
                block, offset, SAMPLE_RATE, 0, 0, 0x34, &analyzed, 1);
        if (result < 0) {
            analyzer_valid = 0;
            break;
        }
        if (result == 1) {
            analyzer_packets += 1;
            analyzer_valid &= analyzed.payload_len == sizeof(expected)
                    && analyzed.spreading_factor == SPREADING_FACTOR
                    && analyzed.bandwidth == BANDWIDTH
                    && !memcmp(analyzed.payload, expected,
                        sizeof(expected));
        }
    }
    analyzer_valid &= analyzer_packets == 1;
    if (!valid || !stream_valid || !analyzer_valid) {
        fprintf(stderr, "lora-fm: synthetic discriminator frame failed\n");
    }
    lora_fm_demod_free(demod);
    free(samples);
    return valid && stream_valid && analyzer_valid ? 0 : 1;
}
