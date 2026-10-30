/** @file
    Proflame2 decoder regression tests and offline bit-row runner.
*/

#include "decoder.h"
#include "output_file.h"
#include <stdlib.h>

extern r_device const proflame2;

static unsigned outputs;
static data_t *last_output;

static void capture_output(r_device *decoder, data_t *data)
{
    (void)decoder;
    ++outputs;
    data_free(last_output);
    last_output = data;
}

static void encode_words(bitbuffer_t *bits, uint8_t const *words, unsigned count)
{
    for (unsigned word = 0; word < count; ++word) {
        unsigned pad     = word == 0;
        unsigned symbols = (words[word] << 3) | (pad << 2) | ((parity8(words[word]) ^ pad) << 1) | 1;
        bitbuffer_add_bit(bits, 1);
        bitbuffer_add_bit(bits, 1);
        bitbuffer_add_bit(bits, 1);
        bitbuffer_add_bit(bits, 0);
        for (int bit = 10; bit >= 0; --bit) {
            unsigned value = (symbols >> bit) & 1;
            bitbuffer_add_bit(bits, value);
            bitbuffer_add_bit(bits, !value);
        }
    }
}

static int run_row(r_device *decoder, bitbuffer_t *bits)
{
    outputs = 0;
    data_free(last_output);
    last_output = NULL;
    return decoder->decode_fn(decoder, bits);
}

int main(int argc, char **argv)
{
    r_device decoder  = proflame2;
    decoder.output_fn = capture_output;
    decoder.verbose   = -1;
    bitbuffer_t bits  = {0};

    if (argc == 2 || argc == 3) {
        bitbuffer_parse(&bits, argv[1]);
        int ret = run_row(&decoder, &bits);
        if (last_output) {
            int pretty = argc == 3 && !strcmp(argv[2], "kv");
            if (pretty)
                last_output = data_prepend(last_output, data_str(NULL, "time", "", NULL, "2026-01-01 12:00:00"));
            data_output_t *output = pretty ? data_output_kv_create(0, stdout) : data_output_json_create(0, stdout);
            data_output_print(output, last_output);
            data_output_free(output);
            data_free(last_output);
        }
        return ret > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    uint8_t words[] = {0x12, 0x34, 0x56, 0x01, 0x06, 0x76, 0xde, 0x00, 0x00, 0x00, 0x00};
    encode_words(&bits, words, 7);
    if (run_row(&decoder, &bits) != 1 || outputs != 1)
        return EXIT_FAILURE;

    bitbuffer_clear(&bits);
    encode_words(&bits, words, 11);
    if (run_row(&decoder, &bits) > 0 || outputs)
        return EXIT_FAILURE;

    uint8_t extended[] = {0x12, 0x34, 0x56, 0x81, 0x20, 0x15, 0xc8, 0x00, 0x21, 0x6e};
    bitbuffer_clear(&bits);
    encode_words(&bits, extended, 10);
    --bits.bits_per_row[0]; // Missing only the physical terminal low half.
    bitbuffer_t original = bits;
    if (run_row(&decoder, &bits) != 1 || outputs != 1 || memcmp(&bits, &original, sizeof(bits)))
        return EXIT_FAILURE;

    // A damaged extended row cannot hide a valid subsequent legacy row.
    bitbuffer_clear(&bits);
    extended[9] ^= 1;
    encode_words(&bits, extended, 10);
    bitbuffer_add_row(&bits);
    encode_words(&bits, words, 7);
    if (run_row(&decoder, &bits) != 1 || outputs != 1)
        return EXIT_FAILURE;

    data_free(last_output);
    return EXIT_SUCCESS;
}
