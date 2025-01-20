/** @file
    Revolt ZX-7717-675 433 MHz power meter.

    Copyright (C) 2024 Christian W. Zuckschwerdt <zany@triq.net>
    Copyright (C) 2024 Boing <dhs_mobil@google.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Revolt ZX-7717-675 433 MHz power meter.

- Used with Revolt ZX-7716 Monitor.
- Other names: HPM-27717, ZX-7717-919
- Up to 6 channels
- First seen: 12-2024
- https://www.revolt-power.de/TOP-KAT161-Zusaetzliche-Steckdose-ZX-7717-919.shtml

Outputs are: (in this order)
- Current (A) max 15.999 A , Minimum is >= 0.001 A
- Voltage (V) max 250.0 V
- Power  (VA) max 3679.9 VA
- PF (Powerfactor not in message, but calculated)
- 8 bit checksum
- some unknown bytes/flags

Modulation: ASK/OOK with Manchester coding.
Send interval: 5 secs and/or when current changes.

HF Output is 10 mW, but appears much higher (due to antenna maybe),
With RSSI -0.1 dB, SNR 33.0 dB at 31 m distance!

The packet is 14 manchester encoded bytes with a Preamble of 0x2A and
an 8-bit checksum (last byte).

Raw data:

    2ab0abe05a15603a14005710840011
    2ab0abe05a15603a13005710df0040
    2ab0abe05a15603ae2c0e710ca20bb
    2ab0abe05a15603a1ac0e710c12078
    2ab0abe05a15603a7d007b104f00c7
    2a88abe05a950026b880603af5c05710d9a018
    2a48abe05a950026b880e000000040000000003e
    2ab0abe05a15603a6ec0b7103f20dd
    2a70abe05a05e08000001c80000000a4

Example messages:

    0d d507 5aa8 06 5c 2800 ea08 2100 88
    0d d507 5aa8 06 5c c800 ea08 fb00 02
    0d d507 5aa8 06 5c 4703 e708 5304 dd
    0d d507 5aa8 06 5c 5803 e708 8304 1e
    0d d507 5aa8 06 5c be00 de08 f200 e3
    11 d507 5aa9 00 64 1d01 065c af03 ea08 9b05 18
    12 d507 5aa9 00 64 1d01 0700 0000 0200 0000 00 7c
    0d d507 5aa8 06 5c 7603 ed08 fc04 bb
    0e d507 5aa0 07 01 0000 3801 0000 00 25

Data layout:

    LL IIII UUUU CC FF AAAA VVVV WWWW XX

- L: (8 bit) 0d    : payload_length (13), excluding the length byte, including checksum
- I: (16 bit) d507    : id
- U: (16 bit) 5aa8    : unknown1
- C: (8 bit) 06    : channel (6) // TODO
- F: (8 bit) 5c    ;  unknown2
- A: (16 bit) be00    : current (0.190)
- V: (16 bit) de08    : voltage    (227.0)
- W: (16 bit) f200    : power    (24.2)
- X: (8 bit) e3    : checksum

*/
static int revolt_zx7717_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x2a}; // sync is 0x2a

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    // message length seen are 0d, 0e, 11, 12, i.e. 13, 14, 17, 18 plus sync and length byte
    unsigned row_len = bitbuffer->bits_per_row[0];
    if (row_len < 15 * 8 || row_len > 31*8) {
        return DECODE_ABORT_EARLY; // Unrecognized data
    }

    unsigned pos = bitbuffer_search(bitbuffer, 0, 0, preamble, 8);
    pos += 8; // skip preamble

    if (pos > 16) { // match only near the start
        return DECODE_ABORT_LENGTH; // preamble not found
    }
    int len = bitbuffer->bits_per_row[0] - pos;

    uint8_t b[32];
    bitbuffer_extract_bytes(bitbuffer, 0, pos, b, len);
    reflect_bytes(b, (len + 7) / 8);

    int msg_len = b[0]; // expected: 0d, 0e, 11, 12, 1b?
    if (msg_len < 1) {
        return DECODE_FAIL_SANITY;
    }

    // Is there enough data for a given length of message?
    if (len < (msg_len + 1) * 8) {
        return DECODE_ABORT_LENGTH; // short buffer
    }

    int sum = add_bytes(b, msg_len);
    if (b[msg_len] != (sum & 0xff)) {
        return DECODE_FAIL_MIC; // bad checksum
    }

    if (msg_len != 13) {
        decoder_log_bitrow(decoder, 1, __func__, b, len, "unhandled message");
        return DECODE_FAIL_OTHER; // unhandled message
    }

    decoder_log_bitrow(decoder, 2, __func__, b, len, "message");

    int id       = (b[1] << 8) | (b[2]); // Big Endian?
    int unknown1 = (b[3] << 8) | b[4];   // Big Endian?
    int unknown2 = (b[5] << 8) | b[6];   // Big Endian?
    int channel  = (b[5]);               // just a guess
    int current  = (b[8] << 8) | b[7];   // Little Endian
    int voltage  = (b[10] << 8) | b[9];  // Little Endian
    int power    = (b[12] << 8) | b[11]; // Little Endian
    // calculation for PF (Powerfactor) is invalid if current is < 0.02 A
    // e.g. a standby device will show bad readings
    // double va = current * voltage * 0.001; // computed value
    // double powerf = va > 1.0 ? power / va : 1.0; // computed value

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Revolt-ZX7717",
            "id",               "Device ID",        DATA_FORMAT, "%04x", DATA_INT, id,
            "channel",          "Channel",          DATA_INT,    channel,
            "unknown_1",        "Unknown 1",        DATA_FORMAT, "%04x", DATA_INT, unknown1,
            "unknown_2",        "Unknown 2",        DATA_FORMAT, "%04x", DATA_INT, unknown2,
            "current_A",        "Current",          DATA_FORMAT, "%.3f A", DATA_DOUBLE, current * 0.001,
            "voltage_V",        "Voltage",          DATA_FORMAT, "%.1f V", DATA_DOUBLE, voltage * 0.1,
            "power_W",          "Power",            DATA_FORMAT, "%.1f W", DATA_DOUBLE, power * 0.1,
            // "apparentpower_VA", "Apparent Power",   DATA_FORMAT, "%.1f VA", DATA_DOUBLE, va * 0.1, // computed value
            // "powerfactor",      "Power Factor",     DATA_DOUBLE, powerf, // computed value
            "mic",              "Integrity",        DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "unknown_1",
        "unknown_2",
        "current_A",
        "voltage_V",
        "power_W",
        // "apparentpower_VA", // computed value
        // "powerfactor", // computed value
        "mic",
        NULL,
};

r_device const revolt_zx7717 = {
        .name        = "Revolt ZX-7717 power meter",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 310, // Nominal width of clock half period [us]
        .long_width  = 310,
        .reset_limit = 900, // Maximum gap size before End Of Message [us]
        .decode_fn   = &revolt_zx7717_decode,
        .fields      = output_fields,
};
