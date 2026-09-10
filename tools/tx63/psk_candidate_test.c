#include "psk_demod.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int infer_sample_rate(char const *filename)
{
    if (strstr(filename, "_1000k")) {
        return 1000000;
    }

    if (strstr(filename, "_250k")) {
        return 250000;
    }

    return 250000;
}

int main(int argc, char **argv)
{
    char const *filename;
    FILE *fp;
    long file_size;
    uint8_t *iq_buf;
    size_t bytes_read;
    size_t sample_count;
    unsigned sample_rate;
    psk_candidate_t candidate;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <capture.cu8>\n", argv[0]);
        return 2;
    }

    filename = argv[1];
    sample_rate = (unsigned)infer_sample_rate(filename);

    fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "ERROR: cannot open %s\n", filename);
        return 2;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 2;
    }

    file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        return 2;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 2;
    }

    iq_buf = malloc((size_t)file_size);
    if (!iq_buf) {
        fclose(fp);
        return 2;
    }

    bytes_read = fread(iq_buf, 1, (size_t)file_size, fp);
    fclose(fp);

    if (bytes_read != (size_t)file_size || (bytes_read & 1)) {
        fprintf(stderr, "ERROR: invalid CU8 capture\n");
        free(iq_buf);
        return 2;
    }

    sample_count = bytes_read / 2;

    printf("Testing %s at %u samples/s\n", filename, sample_rate);

    if (!psk_find_candidate(
                iq_buf,
                sample_count,
                2,
                sample_rate,
                &candidate)) {
        fprintf(stderr, "No PSK candidate found.\n");
        free(iq_buf);
        return 1;
    }

    printf(
            "candidate t=%.6f s  signature=%.1f dB  "
            "carrier_offset=%.1f Hz  symbol_hint=%.1f\n",
            candidate.offset_s,
            candidate.signature_db,
            candidate.carrier_offset_hz,
            candidate.symbol_rate_hint);

    free(iq_buf);
    return 0;
}
