/** @file
    Forever Young RTH11 "RF433 Temp & Humidity Sensor".

    Copyright (C) 2026 dktzde

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

#define RTH11_BITS          80
#define RTH11_PREAMBLE_BITS 31

/**
Forever Young RTH11 "RF433 Temp & Humidity Sensor".

Sold by Shenzhen Forever Young Technology Co., Ltd as a sub device of the Tuya Wi-Fi weather station
(gateway WS06, "P80-FLY RTH11", also sold as eMylo RTH11). The label reads
"RF433 Temp & Humidity Sensor, Model: RTH11, Batch Number A2604", 2 x AAA, IP65.
The sensor sends every hour, or immediately when the temperature changes by 0.5 K or more
or the humidity by 5 % or more.

Flex decoder equivalent (without the CRC check, which flex cannot do):

    rtl_433 -f 433.92M -X "n=RTH11,m=FSK_PCM,s=416,l=416,r=4000,preamble={31}a5294a52,symbol_zero={2}4,symbol_one={3}2,unique"

Modulation:

- FSK, 433.92 MHz, the pulse (F1) is always about 416 us long,
  the gap (F2) is 416 us for a 0 bit and 832 us for a 1 bit (PPM on the gap).
- There is no FSK_PULSE_PPM, so the signal is sliced as FSK_PULSE_PCM with a clock of 416 us
  and the PPM symbols (01 = 0, 001 = 1) are decoded here.
- A burst starts with a sync pulse of 3.2 ms + 3.2 ms gap, then the 80 bit packet is repeated up to 9 times,
  each repeat preceded by a sync pulse of 1.3 to 1.5 ms + 3.2 ms gap.

Packet layout, 80 bits, each byte is sent LSB first (shown here after reversing each byte):

    PP HH LL II II TT FT HH CC KK

- P: preamble, 0xaa (bits 0101 0101 0101 as sent), the 12 bits are 0x555
- H: 0x0a 0x00, constant in all packets seen so far (probably packet type / length)
- I: 16 bit sensor ID, stays the same after a battery change
- T: temperature low byte, F: flags (high nibble), temperature high nibble (low nibble)
  The temperature is a 12 bit signed value in 0.1 C. For negative temperatures the flags nibble is 0xf.
- H: humidity in % (bits 0..6), bit 7 is set for a packet or two after the reset button is pressed
- C: packet counter, +1 for each new measurement, 1 after (re)start
- K: CRC-8, poly 0x59, init 0x00, over bytes 2..8, XOR 0xe4

The long gap of the very last bit is sometimes lost at the end of a transmission.
If the last bit reads 0 (or is missing) and the CRC only matches with a 1, the packet is accepted.

Flags (high nibble of byte 6) as observed:

- 0x4: normal operation
- 0x6: the first packets after power up (battery inserted or restart after under-voltage)
- 0x8: battery low (seen with nearly empty batteries shortly before the sensor stops sending)
- 0x0: seen from time to time on one sensor, meaning unknown
- 0xf: temperature is negative, the flags are not visible then
*/

static int forever_young_rth11_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    static uint8_t const preamble[] = {0xa5, 0x29, 0x4a, 0x52}; // 12 x "01" / "001" symbols, 0x555 as sent
    uint8_t b[10];

    for (unsigned row = 0; row < (unsigned)bitbuffer->num_rows; ++row) {
        unsigned len = bitbuffer->bits_per_row[row];
        unsigned pos = 0;
        if (len < RTH11_PREAMBLE_BITS + 68) {
            continue;
        }

        // Search for the preamble, a row can hold several repeats of the packet.
        while ((pos = bitbuffer_search(bitbuffer, row, pos, preamble, RTH11_PREAMBLE_BITS)) + RTH11_PREAMBLE_BITS < len) {
            unsigned start = pos;
            pos += 1; // continue behind this match if the packet turns out invalid

            // Decode the PPM symbols behind the preamble (the preamble ends with the pulse of the 12th bit).
            unsigned p = start + RTH11_PREAMBLE_BITS;
            unsigned n = 12;
            memset(b, 0, sizeof(b));
            b[0] = 0x55; // the 12 preamble bits 0x555 as sent, the remaining 68 bits are decoded below
            b[1] = 0x50;
            for (; n < RTH11_BITS; ++n) {
                unsigned zeros = 0;
                while (p < len && bitrow_get_bit(bitbuffer->bb[row], p) == 0) {
                    ++zeros;
                    ++p;
                }
                if (p >= len) {
                    break; // symbol not terminated by a pulse
                }
                ++p; // the pulse
                if (zeros < 1 || zeros > 2) {
                    break;
                }
                if (zeros == 2) {
                    b[n / 8] |= 0x80 >> (n % 8);
                }
            }
            // The gap of the very last bit is sometimes not seen at the end of a row,
            // accept a missing last bit here (it stays 0) and let the CRC decide below.
            if (n < RTH11_BITS - 1) {
                continue; // too short
            }
            reflect_bytes(b, sizeof(b));

            if (b[0] != 0xaa) {
                continue;
            }

            uint8_t crc = crc8(&b[2], 7, 0x59, 0x00) ^ 0xe4;
            // A long last gap (1 bit) can be lost or read as a short one at the end of the packet:
            // if the last bit (MSB of the CRC byte after reflecting) is 0 and the CRC matches with 1, accept it.
            if (crc != b[9] && !(b[9] & 0x80) && crc == (b[9] | 0x80)) {
                decoder_log(decoder, 2, __func__, "Last bit restored by CRC");
                b[9] |= 0x80;
            }
            if (crc != b[9]) {
                decoder_logf(decoder, 2, __func__, "CRC mismatch, calculated %02x, received %02x", crc, b[9]);
                continue;
            }

            int id          = (b[3] << 8) | b[4];
            int temp_raw    = ((b[6] & 0x0f) << 8) | b[5];
            int flags       = b[6] >> 4;
            int humidity    = b[7] & 0x7f;
            int button      = b[7] >> 7;
            int counter     = b[8];
            if (temp_raw > 2047) {
                temp_raw -= 4096;
            }
            float temp_c = temp_raw * 0.1f;

            // Sanity: specified -20 to 60 C, 0 to 99 %
            if (temp_c < -40.0f || temp_c > 80.0f || humidity > 100) {
                decoder_logf(decoder, 2, __func__, "Implausible values %.1f C, %d %%", temp_c, humidity);
                continue;
            }

            // The flags are not visible for negative temperatures.
            int battery_known = temp_raw >= 0;
            int battery_low   = flags & 0x8;

            /* clang-format off */
            data_t *data = data_make(
                    "model",            "",                 DATA_STRING, "ForeverYoung-RTH11",
                    "id",               "ID",               DATA_INT,    id,
                    "battery_ok",       "Battery",          DATA_COND,   battery_known, DATA_INT, !battery_low,
                    "temperature_C",    "Temperature",      DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    "button",           "Button",           DATA_COND,   button, DATA_INT, button,
                    "counter",          "Counter",          DATA_INT,    counter,
                    "flags",            "Flags",            DATA_FORMAT, "%x", DATA_INT, flags,
                    "mic",              "Integrity",        DATA_STRING, "CRC",
                    NULL);
            /* clang-format on */

            decoder_output_data(decoder, data);
            return 1;
        }
    }
    return DECODE_ABORT_EARLY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "temperature_C",
        "humidity",
        "button",
        "counter",
        "flags",
        "mic",
        NULL,
};

r_device const forever_young_rth11 = {
        .name        = "Forever Young RTH11 temperature and humidity sensor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 416,
        .long_width  = 416,
        .reset_limit = 4000,
        .decode_fn   = &forever_young_rth11_decode,
        .fields      = output_fields,
};
