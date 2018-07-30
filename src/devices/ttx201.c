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
 * Manchester Encoding, pulse width: 460 us, gap width 1508 us.
 *
 * A complete message is 444 bits:
 *   PPPPPPPP PPPP
 *     KKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM JJJJJJJJ  (repeated 7 times)
 *     KKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM      (last packet without J)
 *
 * 20-bit initial preamble, always 0
 *   PPPPPPPP PPPP = 0x0000 0x00
 *
 * 54-bit data packet format
 *   0   1    2   3    4   5    6   7    8   9    10  11   12  13  (nibbles #, aligned to 8-bit)
 *   ..KKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM JJJJJJJJ
 *
 *   K = 6-bit checksum, sum of nibbles 2-11
 *   I = 8-bit sensor ID
 *   S = startup (0 = normal operation, 1 = reset or battery changed)
 *   ? = unknown, always 0
 *   B = battery status (0 = OK, 1 = low)
 *   C = 3-bit channel, 0-4
 *   X = 3-bit packet index, 0-7
 *   T = 12-bit signed temperature * 10 in Celsius
 *   M = 8-bit postmark, always 0x14
 *   J = 8-bit packet separator, always 0xF8
 *
 * Sample received raw data package:
 *   bitbuffer:: Number of rows: 1
 *   [00] {444} 00 00 06 f0 80 00 41 c5 3e 1c c2 00 11 07 14 f8 77 08 00 84 1c 53 e1 ec 20 03 10 71 4f 87 f0 80 10 41 c5 3e 20 c2 00 51 07 14 f8 87 08 01 84 1c 53 e2 2c 20 07 10 71 40 
 *
 * Decoded:
 *   K   I    S    B  C    X   T    M     J
 *   27  194  0x0  0  0x0  0   263  0x14  0xf8
 *   28  194  0x0  0  0x0  1   263  0x14  0xf8
 *   29  194  0x0  0  0x0  2   263  0x14  0xf8
 *   30  194  0x0  0  0x0  3   263  0x14  0xf8
 *   31  194  0x0  0  0x0  4   263  0x14  0xf8
 *   32  194  0x0  0  0x0  5   263  0x14  0xf8
 *   33  194  0x0  0  0x0  6   263  0x14  0xf8
 *   34  194  0x0  0  0x0  7   263  0x14
 */

#include "rtl_433.h"
#include "data.h"
#include "util.h"

#define MODEL                   "TTX201 Temperature Sensor"
#define MSG_BITS                444
#define MSG_PREAMBLE_BITS       20
#define MSG_PACKET_BITS         54
#define MSG_PACKET_POSTMARK     0x14
#define MSG_PACKET_SEPARATOR    0xf8
#define TEMP_NULL               (-2731)

#ifndef CHAR_BIT
#define CHAR_BIT                8
#endif

#define BITLEN(x)               (sizeof(x) * CHAR_BIT)
#define MSG_PAD_BITS            ((((MSG_PACKET_BITS / CHAR_BIT) + 1) * CHAR_BIT) - MSG_PACKET_BITS)
#define MSG_DATA_BITS           (MSG_PAD_BITS + MSG_PACKET_BITS - BITLEN(packet_end))
#define MSG_LEN                 ((MSG_BITS + CHAR_BIT - 1) / CHAR_BIT)
#define MSG_MIN_BITS            (MSG_PREAMBLE_BITS + 2 * MSG_PACKET_BITS)

#define SWAP_UINT16(x) ((uint16_t) (                           \
                         (((uint16_t) (x) & 0x00ff) << 8) |    \
                         (((uint16_t) (x) & 0xff00) >> 8)      \
                       ))
#define SWAP_UINT32(x) ((uint32_t) (                           \
                         (((uint32_t) (x) & 0xff00) << 8) |    \
                         (((uint32_t) (x) & 0xff0000) >> 8) |  \
                         (((uint32_t) (x) & 0xff) << 24) |     \
                         ((uint32_t) (x) >> 24)                \
                       ))

static const
uint8_t packet_end[2] = {MSG_PACKET_POSTMARK, MSG_PACKET_SEPARATOR}; // 16 bits

/*
 * count leading zero bits
 */
static int clz(uint32_t x)
{
    int i;

    for (i = BITLEN(uint32_t) - 1; i >= 0 && ((x >> i) & 1) == 0; i--) {
        // counting...
    }

    return BITLEN(uint32_t) - 1 - i;
}

#define HIGH(x) (((x) & 0xf0) >> 4)
#define LOW(x)  ((x) & 0x0f)
static int
checksum_calculate(uint8_t *b)
{
    int i;
    int sum = 0;

    for (i = 1; i < 6; i++) {
        sum += HIGH(b[i]) + LOW(b[i]);
    }
  
    return sum & 0x3f;  
}


static int
ttx201_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[MSG_LEN];
    uint32_t *bi = (uint32_t *) b;
    int bits = bitbuffer->bits_per_row[row];
    int preamble = 0;
    int offset = 0;
    int valid_packets = 0;
    int checksum;
    int checksum_calculated;
    int postmark;
    int device_id;
    int battery_low;
    int channel;
    int temp_last;
    int temperature = TEMP_NULL;
    float temperature_c;
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;

    if (bits < MSG_MIN_BITS) {
        return 0;
    }

    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, BITLEN(uint32_t));
    if (bi[0] != 0) {
        preamble = clz(SWAP_UINT32(bi[0]));
    }

    if (debug_output) {
        printf("TTX201 received raw data: ");
        bitbuffer_print(bitbuffer);
        printf("Preamble: 0x%08x, length: %d\n", SWAP_UINT32(bi[0]), preamble);

        if (bits != MSG_BITS) {
            printf("Wrong message length: %d (expected %d)\n", bits, MSG_BITS);
        }
        if (preamble < MSG_PREAMBLE_BITS) {
            printf("Short preamble: %d bits (expected %d)\n", preamble, MSG_PREAMBLE_BITS);
        }
        printf("Data decoded:\n a   v  cs    K   ID    S   B  C  X    T    M     J\n");
    }
 
    if (preamble > MSG_PREAMBLE_BITS) {
        preamble = MSG_PREAMBLE_BITS;
    } else if (preamble < MSG_PAD_BITS) {
        preamble = MSG_PAD_BITS;
    }

    // walk packets
    for (offset = preamble - MSG_PAD_BITS;
         offset >= 0 && offset < bits - MSG_PACKET_BITS + ((int) BITLEN(packet_end) / 2);
         offset += MSG_PACKET_BITS) {

        bitbuffer_extract_bytes(bitbuffer, row, bitpos + offset, b, MSG_PACKET_BITS);

        /* Aligned data: ..KKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM JJJJJJJJ */
        checksum = b[0] & 0x3f;
        checksum_calculated = checksum_calculate(b);
        postmark = b[5];

        if (debug_output) {
            printf("%3d %2d  %2d    %2d  %3d  0x%01x  %1d  %1d  %1d  %4d  0x%02x",
			offset,
			valid_packets,
			checksum_calculated,
			checksum,
			b[1],				// Device Id
			(b[2] & 0xf0) >> 4,		// Unknown 1
			(b[2] & 0x08) >> 3,		// Battery
			b[2] & 0x07,			// Channel
			b[3] >> 4,			// Packet index
			((int8_t)((b[3] & 0x0f) << 4) << 4) | b[4],	// Temperature
			postmark
                );
            if (offset < MSG_BITS - MSG_PACKET_BITS) {
                printf("  0x%02x", b[6]);		// Packet separator
            }
            printf("\n");
        }

        if (postmark == MSG_PACKET_POSTMARK && \
            checksum == checksum_calculated) {

            device_id = b[1];
            battery_low = (b[2] & 0x08) != 0; // if not zero, battery is low
            channel = (b[2] & 0x07) + 1;
            temp_last = temperature;
            temperature = ((int8_t)((b[3] & 0x0f) << 4) << 4) | b[4]; // note the sign extend

            if (temp_last == temperature || temp_last == TEMP_NULL) {
                valid_packets++;
            } else if (valid_packets > 2) {
                temperature = temp_last; // maybe invalid, recover previous
            }

        } else {
            // search valid packet
            offset = bitbuffer_search(bitbuffer, row,
                         bitpos + offset + MSG_DATA_BITS + 1,
                         (const uint8_t *)&packet_end, BITLEN(packet_end));

            if (debug_output) {
                printf("Checksum error: %d x %d, postmark 0x%02x, end: %d\n",
				checksum, checksum_calculated, postmark, offset);
            }

            if (offset >= MSG_BITS) {
                break;
            }
            offset -= MSG_DATA_BITS + MSG_PACKET_BITS;
        }
    }

    if (valid_packets < 1) {
        return 0;
    }

    temperature_c = temperature / 10.0f;
 
    local_time_str(0, time_str);
    data = data_make(
            "time",           "",             DATA_STRING, time_str,
            "model",          "",             DATA_STRING, MODEL,
            "id",             "House Code",   DATA_INT,    device_id,
            "channel",        "Channel",      DATA_INT,    channel,
            "battery",        "Battery",      DATA_STRING, battery_low ? "LOW" : "OK",
            "temperature_C",  "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature_c,
            NULL);
    data_acquired_handler(data);

    return 1;
}

static int
ttx201_callback(bitbuffer_t *bitbuffer)
{
    int row;
    int events = 0;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] > MSG_PACKET_BITS) {
            events += ttx201_decode(bitbuffer, row, 0);
        }
        if (events) return events; // for now, break after first successful message
    }

    return events;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    NULL
};

r_device ttx201 = {
    .name          = MODEL,
    .modulation    = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit   = 510,
    .long_limit    = 0, // not used
    .reset_limit   = 1700,
    .json_callback = &ttx201_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields
};

