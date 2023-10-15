/** @file
    Emos TTX201 Thermo Remote Sensor.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Emos TTX201 Thermo Remote Sensor.

Manufacturer: Ewig Industries Macao
Maybe same as Ewig TTX201M (FCC ID: N9ZTTX201M)

IROX ETS69 temperature sensor with DCF77 receiver for EBR606C weather station (Ewig WSA101)
uses the same protocol. It transmits temperature the same way as TTX201 (except for different M bits).
If its internal clock is synchronized to DCF77, it transmits the date/time every hour (:00) instead of
the temperature. The date/time is also transmitted after clock is synced at startup.

Transmit Interval: every ~61 s
Frequency: 433.92 MHz
Manchester Encoding, pulse width: 500 us, interpacket gap width 1500 us.

A complete message is 445 bits:

       PPPPPPPP PPPPPPPP P
    LL LLKKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM JJJJ  (repeated 7 times)
    LL LLKKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM       (last packet without J)

17-bit initial preamble, always 0

    PPPPPPPP PPPPPPPP P = 0x00 0x00 0

54-bit data packet format

    0    1   2    3   4    5   6    7   8    9   10   11  12   13  (nibbles #, aligned to 8-bit values)
    ..LL LLKKKKKK IIIIIIII StttBCCC 0XXXTTTT TTTTTTTT MMMMMMMM JJJJ	(temperature)
or  ..LL LLKKKKKK zyyyyyyy 0tttmmmm dddddHHH HHMMMMMM 0SSSSSS? JJJJ	(date/time)

- L = 4-bit start of packet, always 0
- K = 6-bit checksum, sum of nibbles 3-12
- I = 8-bit sensor ID
- S = startup (0 = normal operation, 1 = reset or battery changed)
- t = data type (000 = temperature, 101 = date/time)
- 0 = unknown, always 0
- B = battery status (0 = OK, 1 = low)
- C = 3-bit channel, 0-4
- X = 3-bit packet index, 0-7
- T = 12-bit signed temperature * 10 in Celsius
- M = 8-bit postmark (sensor model?), always 0x14 for TTX201, 0x00 for ETS69
- J = 4-bit packet separator, always 0xF

date/time bit definitions:
- z = time zone/summer time (0 = CET, 1 = CEST)
- y = year
- m = month
- d = day
- H = hour
- M = minute
- S = second
- ? = purpose unknown, always 0 or 1, changes only after reset (battery change)

Sample received raw data package:

    bitbuffer:: Number of rows: 10
    [00] {17} 00 00 00             : 00000000 00000000 0
    [01] {54} 07 30 80 00 42 05 3c
    [02] {54} 07 70 80 04 42 05 3c
    [03] {54} 07 b0 80 08 42 05 3c
    [04] {54} 07 f0 80 0c 42 05 3c
    [05] {54} 08 30 80 10 42 05 3c
    [06] {54} 08 70 80 14 42 05 3c
    [07] {54} 08 b0 80 18 42 05 3c
    [08] {50} 08 f0 80 1c 42 05 00 : 00001000 11110000 10000000 00011100 01000010 00000101 00
    [09] { 1} 00                   : 0

Data decoded:

    r  cs    K   ID    S   B  C  X    T    M     J
    1  28    28  194  0x0  0  0  0   264  0x14  0xf
    2  29    29  194  0x0  0  0  1   264  0x14  0xf
    3  30    30  194  0x0  0  0  2   264  0x14  0xf
    4  31    31  194  0x0  0  0  3   264  0x14  0xf
    5  32    32  194  0x0  0  0  4   264  0x14  0xf
    6  33    33  194  0x0  0  0  5   264  0x14  0xf
    7  34    34  194  0x0  0  0  6   264  0x14  0xf
    8  35    35  194  0x0  0  0  7   264  0x14
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

#define DATA_TYPE_TEMP       0x00
#define DATA_TYPE_DATETIME   0x05

static int checksum_calculate(uint8_t *b)
{
    int i;
    int sum = 0;

    for (i = 1; i < 6; i++) {
        sum += ((b[i] & 0xf0) >> 4) + (b[i] & 0x0f);
    }
    return sum & 0x3f;
}

static int ttx201_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[MSG_PACKET_LEN];
    int bits = bitbuffer->bits_per_row[row];
    int checksum;
    int checksum_calculated;
    int data_type;
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
                    decoder_logf(decoder, 2, __func__, "Short preamble: %d bits (expected %d)",
                            bits, MSG_PREAMBLE_BITS);
                }
            } else if (row != (unsigned)bitbuffer->num_rows - 1 && bits == 1) {
                decoder_logf(decoder, 2, __func__, "Wrong packet #%u length: %d bits (expected %d)",
                        row, bits, MSG_PACKET_BITS);
            }
        }
        return DECODE_ABORT_LENGTH;
    }

    bitbuffer_extract_bytes(bitbuffer, row, bitpos + MSG_PAD_BITS, b, MSG_PACKET_BITS + MSG_PAD_BITS);

    /* Aligned data: LLKKKKKK IIIIIIII S???BCCC ?XXXTTTT TTTTTTTT MMMMMMMM JJJJ */
    checksum = b[0] & 0x3f;
    checksum_calculated = checksum_calculate(b);
    data_type = (b[2] & 0x70) >> 4;
    postmark = b[5];

    if (decoder->verbose > 1) {
        decoder_log(decoder, 0, __func__, "TTX201 received raw data");
        decoder_log_bitbuffer(decoder, 0, __func__, bitbuffer, "");
        decoder_logf(decoder, 0, __func__, "Data decoded:" \
                " r  cs    K   ID    S   B  C  X    T    M     J\n");
        decoder_logf(decoder, 0, __func__, "%2u  %2d    %2d  %3d  0x%01x  %1d  %1d  %1d  %4d  0x%02x",
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
            decoder_logf(decoder, 0, __func__, "  0x%01x", b[6] >> 4);         // Packet separator
        }
        decoder_log(decoder, 0, __func__, "");
    }

    if (checksum != checksum_calculated) {
        decoder_logf(decoder, 2, __func__, "Packet #%u checksum error.", row);
        return DECODE_FAIL_MIC;
    }

    if (data_type == DATA_TYPE_DATETIME) {
        int cest = b[1] & 0x80;
        int year = b[1] & 0x7f;
        int month = b[2] & 0x0f;
        int day = (b[3] & 0xf8) >> 3;
        int hour = (b[3] & 0x07) << 2 | (b[4] & 0xc0) >> 6;
        int minute = b[4] & 0x3f;
        int second = (b[5] & 0x7e) >> 1;
        char clock_str[25];
        snprintf(clock_str, sizeof(clock_str), "%04d-%02d-%02dT%02d:%02d:%02d %s", year + 2000, month, day, hour, minute, second, cest ? "CEST" : "CET");

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Emos-TTX201",
                "radio_clock",      "Radio Clock",  DATA_STRING, clock_str,
                "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */
    } else { // temperature
        device_id = b[1];
        battery_low = (b[2] & 0x08) != 0; // if not zero, battery is low
        channel = (b[2] & 0x07) + 1;
        temperature   = (int16_t)(((b[3] & 0x0f) << 12) | (b[4] << 4)); // uses sign extend
        temperature_c = (temperature >> 4) * 0.1f;

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Emos-TTX201",
                "id",               "House Code",   DATA_INT,    device_id,
                "channel",          "Channel",      DATA_INT,    channel,
                "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature_c,
                "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */
    }

    decoder_output_data(decoder, data);
    return 1;
}

/**
Emos TTX201 Thermo Remote Sensor.
@sa ttx201_decode()
*/
static int ttx201_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row;
    int ret    = 0;
    int events = 0;

    if (MSG_MIN_ROWS <= bitbuffer->num_rows && bitbuffer->num_rows <= MSG_MAX_ROWS) {
        for (row = 0; row < bitbuffer->num_rows; ++row) {
            ret = ttx201_decode(decoder, bitbuffer, row, 0);
            if (ret > 0)
                events += ret;
            if (events && !decoder->verbose)
                return events; // for now, break after first successful message
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "mic",
        "radio_clock",
        NULL,
};

r_device const ttx201 = {
        .name        = "Emos TTX201 Temperature Sensor",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 510,
        .long_width  = 0, // not used
        .reset_limit = 1700,
        .tolerance   = 250,
        .decode_fn   = &ttx201_callback,
        .fields      = output_fields,
};
