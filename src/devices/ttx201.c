/*
 * Emos TTX201 Thermo Remote Sensor
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Manufacturer: Ewig Industries Macao
 * Maybe same as Ewig TTX201M (FCC ID: N9ZTTX201M)
 *
 * Transmit Interval: every ~61 s
 * Frequency: 433.92 MHz
 * Manchester Encoding, pulse width: 500 us, interpacket gap width 1500 us.
 *
 * A complete message is 445 bits:
 *      PPPPPPPP PPPPPPPP P
 *   LL LLKKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM JJJJ  (repeated 7 times)
 *   LL LLKKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM       (last packet without J)
 *
 * 17-bit initial preamble, always 0
 *   PPPPPPPP PPPPPPPP P = 0x00 0x00 0
 *
 * 54-bit data packet format
 *   0    1   2    3   4    5   6    7   8    9   10   11  12   13  (nibbles #, aligned to 8-bit values)
 *   ..LL LLKKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM JJJJ
 *
 *   L = 4-bit start of packet, always 0
 *   K = 6-bit checksum, sum of nibbles 3-12
 *   I = 8-bit sensor ID
 *   S = startup (0 = normal operation, 1 = reset or battery changed)
 *   ? = unknown, always 0
 *   B = battery status (0 = OK, 1 = low)
 *   C = 3-bit channel, 0-4
 *   X = 3-bit packet index, 0-7
 *   T = 12-bit signed temperature * 10 in Celsius
 *   M = 8-bit postmark, always 0x14
 *   J = 4-bit packet separator, always 0xF
 *
 * Sample received raw data package:
 *   bitbuffer:: Number of rows: 10
 *   [00] {17} 00 00 00             : 00000000 00000000 0
 *   [01] {54} 07 30 80 00 42 05 3c
 *   [02] {54} 07 70 80 04 42 05 3c
 *   [03] {54} 07 b0 80 08 42 05 3c
 *   [04] {54} 07 f0 80 0c 42 05 3c
 *   [05] {54} 08 30 80 10 42 05 3c
 *   [06] {54} 08 70 80 14 42 05 3c
 *   [07] {54} 08 b0 80 18 42 05 3c
 *   [08] {50} 08 f0 80 1c 42 05 00 : 00001000 11110000 10000000 00011100 01000010 00000101 00
 *   [09] { 1} 00                   : 0
 *
 * Data decoded:
 *   r  cs    K   ID    S   B  C  X    T    M     J
 *   1  28    28  194  0x0  0  0  0   264  0x14  0xf
 *   2  29    29  194  0x0  0  0  1   264  0x14  0xf
 *   3  30    30  194  0x0  0  0  2   264  0x14  0xf
 *   4  31    31  194  0x0  0  0  3   264  0x14  0xf
 *   5  32    32  194  0x0  0  0  4   264  0x14  0xf
 *   6  33    33  194  0x0  0  0  5   264  0x14  0xf
 *   7  34    34  194  0x0  0  0  6   264  0x14  0xf
 *   8  35    35  194  0x0  0  0  7   264  0x14
 */

#include "decoder.h"

#define MSG_PREAMBLE_BITS    17
#define MSG_PACKET_MIN_BITS  50
#define MSG_PACKET_BITS      54
#define MSG_PACKET_POSTMARK  0x14
#define MSG_MIN_ROWS         2
#define MSG_MAX_ROWS         10

#define MSG_PAD_BITS         ((((MSG_PACKET_BITS / 8) + 1) * 8) - MSG_PACKET_BITS)
#define MSG_PACKET_LEN       ((MSG_PACKET_BITS + MSG_PAD_BITS) / 8)

static int
checksum_calculate(uint8_t *b)
{
    int i;
    int sum = 0;

    for (i = 1; i < 6; i++) {
        sum += ((b[i] & 0xf0) >> 4) + (b[i] & 0x0f);
    }
    return sum & 0x3f;
}

static int
ttx201_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[MSG_PACKET_LEN];
    int bits = bitbuffer->bits_per_row[row];
    int checksum;
    int checksum_calculated;
    int postmark;
    int device_id;
    int battery_low;
    int channel;
    int temperature;
    float temperature_c;
    data_t *data;

    if (bits != MSG_PACKET_MIN_BITS && bits != MSG_PACKET_BITS) {
        if (decoder->verbose > 1) {
            if (row == 0) {
                if (bits < MSG_PREAMBLE_BITS) {
                    fprintf(stderr, "Short preamble: %d bits (expected %d)\n",
                            bits, MSG_PREAMBLE_BITS);
                }
            } else if (row != (unsigned)bitbuffer->num_rows - 1 && bits == 1) {
                fprintf(stderr, "Wrong packet #%d length: %d bits (expected %d)\n",
                        row, bits, MSG_PACKET_BITS);
            }
        }
        return 0;
    }

    bitbuffer_extract_bytes(bitbuffer, row, bitpos + MSG_PAD_BITS, b, MSG_PACKET_BITS + MSG_PAD_BITS);

    /* Aligned data: LLKKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM JJJJ */
    checksum = b[0] & 0x3f;
    checksum_calculated = checksum_calculate(b);
    postmark = b[5];

    if (decoder->verbose > 1) {
        fprintf(stderr, "TTX201 received raw data: ");
        bitbuffer_print(bitbuffer);
        fprintf(stderr, "Data decoded:\n" \
                " r  cs    K   ID    S   B  C  X    T    M     J\n");
        fprintf(stderr, "%2d  %2d    %2d  %3d  0x%01x  %1d  %1d  %1d  %4d  0x%02x",
                row,
                checksum_calculated,
                checksum,
                b[1],                                       // Device Id
                (b[2] & 0xf0) >> 4,                         // Unknown 1
                (b[2] & 0x08) >> 3,                         // Battery
                b[2] & 0x07,                                // Channel
                b[3] >> 4,                                  // Packet index
                ((int8_t)((b[3] & 0x0f) << 4) << 4) | b[4], // Temperature
                postmark);
        if (bits == MSG_PACKET_BITS) {
            fprintf(stderr, "  0x%01x", b[6] >> 4);         // Packet separator
        }
        fprintf(stderr, "\n");
    }

    if (postmark != MSG_PACKET_POSTMARK) {
        if (decoder->verbose > 1)
            fprintf(stderr, "Packet #%d wrong postmark 0x%02x (expected 0x%02x).\n",
                    row, postmark, MSG_PACKET_POSTMARK);
        return 0;
    }

    if (checksum != checksum_calculated) {
        if (decoder->verbose > 1)
            fprintf(stderr, "Packet #%d checksum error.\n", row);
        return 0;
    }

    device_id = b[1];
    battery_low = (b[2] & 0x08) != 0; // if not zero, battery is low
    channel = (b[2] & 0x07) + 1;
    temperature   = (int16_t)(((b[3] & 0x0f) << 12) | (b[4] << 4)); // uses sign extend
    temperature_c = (temperature >> 4) * 0.1f;

    data = data_make(
            "model",         "",            DATA_STRING, _X("Emos-TTX201","Emos TTX201"),
            "id",            "House Code",  DATA_INT,    device_id,
            "channel",       "Channel",     DATA_INT,    channel,
            "battery",       "Battery",     DATA_STRING, battery_low ? "LOW" : "OK",
            "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature_c,
            "mic",           "MIC",         DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static int
ttx201_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row;
    int events = 0;

    if (MSG_MIN_ROWS <= bitbuffer->num_rows && bitbuffer->num_rows <= MSG_MAX_ROWS) {
        for (row = 0; row < bitbuffer->num_rows; ++row) {
            events += ttx201_decode(decoder, bitbuffer, row, 0);
            if (events && !decoder->verbose) return events; // for now, break after first successful message
        }
    }

    return events;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    "mic",
    NULL
};

r_device ttx201 = {
    .name          = "Emos TTX201 Temperature Sensor",
    .modulation    = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width   = 510,
    .long_width    = 0, // not used
    .reset_limit   = 1700,
    .tolerance     = 250,
    .decode_fn     = &ttx201_callback,
    .disabled      = 0,
    .fields        = output_fields
};

