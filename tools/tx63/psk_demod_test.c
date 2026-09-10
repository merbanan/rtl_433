#include "psk_demod.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t frame[9];
} tx63_validate_context_t;

static uint16_t crc16_x25(uint8_t const *data, size_t len)
{
    uint16_t crc = 0xffff;
    size_t i;

    for (i = 0; i < len; ++i) {
        int bit;

        crc ^= data[i];

        for (bit = 0; bit < 8; ++bit) {
            if (crc & 1) {
                crc = (uint16_t)((crc >> 1) ^ 0x8408);
            }
            else {
                crc >>= 1;
            }
        }
    }

    return (uint16_t)(crc ^ 0xffff);
}

static int flag_at(
        uint8_t const *row,
        size_t bit_count,
        size_t pos)
{
    static uint8_t const flag[8] = {
            0, 1, 1, 1, 1, 1, 1, 0};
    size_t i;

    if (pos + 8 > bit_count) {
        return 0;
    }

    for (i = 0; i < 8; ++i) {
        if (bitrow_get_bit(row, (unsigned)(pos + i)) != flag[i]) {
            return 0;
        }
    }

    return 1;
}

static int destuff_frame(
        uint8_t const *row,
        size_t start,
        size_t end,
        uint8_t frame[9])
{
    uint8_t bits[72];
    size_t bit_count = 0;
    unsigned ones = 0;
    size_t i;

    memset(frame, 0, 9);

    for (i = start; i < end; ++i) {
        uint8_t bit = bitrow_get_bit(row, (unsigned)i);

        /*
         * HDLC inserts a zero after five consecutive one bits.
         * That stuffed zero is not part of the payload.
         */
        if (ones == 5) {
            if (bit != 0) {
                return 0;
            }

            ones = 0;
            continue;
        }

        if (bit_count >= 72) {
            return 0;
        }

        bits[bit_count++] = bit;

        if (bit) {
            ++ones;
        }
        else {
            ones = 0;
        }
    }

    if (bit_count != 72) {
        return 0;
    }

    /*
     * TX63 HDLC payload bytes are transmitted LSB first.
     */
    for (i = 0; i < 72; ++i) {
        if (bits[i]) {
            frame[i / 8] |=
                    (uint8_t)(1U << (i % 8));
        }
    }

    return 1;
}

static int find_valid_frame(
        bitbuffer_t const *bitbuffer,
        uint8_t frame[9])
{
    uint8_t const *row;
    size_t bit_count;
    size_t start_flag;

    if (!bitbuffer || bitbuffer->num_rows < 1) {
        return 0;
    }

    row = bitbuffer->bb[0];
    bit_count = bitbuffer->bits_per_row[0];

    for (start_flag = 0;
            start_flag + 8 <= bit_count;
            ++start_flag) {
        size_t end_flag;

        if (!flag_at(row, bit_count, start_flag)) {
            continue;
        }

        /*
         * This is the validated TX63 search range from the standalone
         * decoder. The separation is measured between flag starts.
         */
        for (end_flag = start_flag + 75;
                end_flag <= start_flag + 110 &&
                end_flag + 8 <= bit_count;
                ++end_flag) {
            uint16_t received_crc;
            uint16_t calculated_crc;

            if (!flag_at(row, bit_count, end_flag)) {
                continue;
            }

            if (!destuff_frame(
                        row,
                        start_flag + 8,
                        end_flag,
                        frame)) {
                continue;
            }

            /*
             * Known TX63 family prefix from all validated captures.
             * This is intentionally in the protocol validator, not
             * in the generic PSK demodulator.
             */
            if (frame[0] != 0x3c || frame[1] != 0xc0) {
                continue;
            }

            received_crc =
                    (uint16_t)frame[7] |
                    ((uint16_t)frame[8] << 8);

            calculated_crc = crc16_x25(frame, 7);

            if (received_crc != calculated_crc) {
                continue;
            }

            return 1;
        }
    }

    return 0;
}

static int tx63_validate(
        bitbuffer_t *bitbuffer,
        void *context)
{
    tx63_validate_context_t *tx63_context = context;

    if (!tx63_context) {
        return 0;
    }

    return find_valid_frame(
            bitbuffer,
            tx63_context->frame);
}

static unsigned infer_sample_rate(char const *filename)
{
    if (strstr(filename, "_1000k")) {
        return 1000000;
    }

    if (strstr(filename, "_250k")) {
        return 250000;
    }

    return 0;
}

static int load_cu8(
        char const *filename,
        uint8_t **data,
        size_t *byte_count)
{
    FILE *fp;
    long length;
    uint8_t *buffer;
    size_t read_count;

    if (!filename || !data || !byte_count) {
        return 0;
    }

    fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Unable to open: %s\n", filename);
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }

    length = ftell(fp);
    if (length <= 0) {
        fclose(fp);
        return 0;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    buffer = malloc((size_t)length);
    if (!buffer) {
        fclose(fp);
        return 0;
    }

    read_count =
            fread(buffer, 1, (size_t)length, fp);

    fclose(fp);

    if (read_count != (size_t)length) {
        free(buffer);
        return 0;
    }

    /*
     * CU8 contains one unsigned I byte followed by one unsigned Q byte.
     */
    if (read_count & 1) {
        --read_count;
    }

    *data = buffer;
    *byte_count = read_count;

    return 1;
}

static char const *direction_name(unsigned code)
{
    static char const *const names[16] = {
            "N", "NNE", "NE", "ENE",
            "E", "ESE", "SE", "SSE",
            "S", "SSW", "SW", "WSW",
            "W", "WNW", "NW", "NNW"};

    return names[code & 0x0f];
}

int main(int argc, char **argv)
{
    char const *filename;
    unsigned sample_rate;
    uint8_t *iq_buf = NULL;
    size_t byte_count = 0;
    size_t sample_count;
    psk_candidate_t candidate;
    bitbuffer_t bitbuffer;
    tx63_validate_context_t validate_context = {{0}};
    uint8_t const *frame;
    unsigned direction_code;
    unsigned speed_raw;
    unsigned gust_direction_code;
    unsigned gust_raw;
    int i;

    if (argc != 2) {
        fprintf(
                stderr,
                "Usage: %s capture.cu8\n",
                argv[0]);
        return 2;
    }

    filename = argv[1];

    sample_rate = infer_sample_rate(filename);
    if (!sample_rate) {
        fprintf(
                stderr,
                "Cannot infer sample rate from filename: %s\n",
                filename);
        return 2;
    }

    if (!load_cu8(
                filename,
                &iq_buf,
                &byte_count)) {
        return 1;
    }

    sample_count = byte_count / 2;

    if (!psk_find_candidate(
                iq_buf,
                sample_count,
                2,
                sample_rate,
                &candidate)) {
        fprintf(stderr, "No PSK candidate found\n");
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

    bitbuffer_clear(&bitbuffer);

    if (!psk_demod_candidate(
                iq_buf,
                sample_count,
                2,
                sample_rate,
                &candidate,
                tx63_validate,
                &validate_context,
                &bitbuffer)) {
        fprintf(stderr, "PSK candidate did not validate\n");
        free(iq_buf);
        return 1;
    }

    frame = validate_context.frame;

    printf("frame=");

    for (i = 0; i < 9; ++i) {
        printf(
                "%s%02X",
                i ? " " : "",
                frame[i]);
    }

    printf("\n");

    direction_code =
            (frame[2] >> 4) & 0x0f;

    speed_raw =
            ((unsigned)(frame[2] & 0x0f) << 8) |
            frame[3];

    gust_direction_code =
            (frame[4] >> 4) & 0x0f;

    gust_raw =
            ((unsigned)(frame[4] & 0x0f) << 8) |
            frame[5];

    printf(
            "wind=%.1f m/s %s  gust=%.1f m/s %s\n",
            speed_raw / 10.0,
            direction_name(direction_code),
            gust_raw / 10.0,
            direction_name(gust_direction_code));

    free(iq_buf);

    return 0;
}