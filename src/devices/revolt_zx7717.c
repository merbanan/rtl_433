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

All data is little endian.
There are 2 message types: 06 is power, 07 is energy.

power:
    0d 7e05 5a 68 06 5c 2100 e108 0700 c5
    000 : (8bit)    0d      Length:13
    001 : (16bit)   7e05    id:1406
    003 : (8bit)    5a      version:90
    004 : (8bit)    68      flags:"01101000"
    005 : (8bit)    06      type:6 (power)
    006 : (8bit)    5c      92?
    007 : (16bit)   2100    current_A:0.033
    009 : (16bit    e108    voltage_V:227.3
    011 : (16bit)   0700    power_W:0.7
    013 : (8bit)    c5        checksum

coldstart power:
    11 d507 5a a9 00 64 1d01 06 5c af03 ea08 9b05 18
    000 : (8bit)    11      Length:17 (coldstart power)
    001 : (16bit)   d507    id:2005
    003 : (8bit)    5a      version:90
    004 : (8bit)    a9      flags:"10101001"
    005 : (8bit)    00      ?0
    006 : (8bit)    64      ?100
    007 : (16bit    1d01    ?285
    009 : (8bit)    06      type:6 (power)
    010 : (8bit)    5c      ?92
    011 : (16bit)   af03    current_A:0.943
    013 : (16bit    ea08    voltage_V:228.2
    015 : (16bit)   9b05    power_W:143.5
    017 : (8bit)    c5      checksum

energy:
    0e d507 5a a0 07 030000 a503 020000 98
    000 : (8bit)    0e      Length:14
    001 : (16bit)   7e05    id:2005
    003 : (8bit)    5a      version:90
    004 : (8bit)    a0      flags:"10100000"
    005 : (8bit)    07      type:7 (energy)
    006 : (24bit)   030000  energy_kWh:0.03
    009 : (16bit    a503    tsec:933 (15min 33sec)
    011 : (24bit)   020000  energy_kWh:0.02
    013 : (8bit)    98      checksum

coldstart energy:
    12 7e 05 5a 69 00 64 1d 01 07 000000 0200 000000 e3
    000 : (8bit)    0e      Length:18
    001 : (16bit)   7e05    id:2005
    003 : (8bit)    5a      version:90
    004 : (8bit)    69      flags:"1101001"
    005 : (16bit)   0064    uke1:    ?0064    (?100)
    007 : (16bit)   1d01    uke2:    ?1d01    (?285)
    009 : (8bit)    07      type:7    (energy)
    010 : (24bit)   000000  energy_kWh:0.00
    013 : (16bit)   0200    tsec:2 (2sec)
    015 : (24bit)   020000  energy_kWh:0.00
    018 : (8bit)    98      checksum

*/
static int revolt_zx7717_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0x2a}; // sync is 0x2a

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }
    // valid message lengths are 0d, 0e, 11, 12, i.e. 13, 14, 17, 18 plus sync and length byte
    unsigned row_len = bitbuffer->bits_per_row[0];
    if (row_len < 15 * 8 || row_len > 22 * 8) {
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

    int msg_len = b[0]; // expected: 0d, 0e, 11, 12
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

    decoder_log_bitrow(decoder, 2, __func__, b, len, "message");

    int is_power   = 0;
    int is_energy  = 0;
    int id         = (b[2] << 8) | (b[1]);
    int version    = (b[3]);
    // int flags      = (b[4]);
    // int type       = 0;
    int current    = 0;
    int voltage    = 0;
    int power      = 0;
    int energy_kWh = 0;
    // int tsec       = 0; // time in secs energy changed

    if (msg_len == 13) {
        // power 0x0d
        // 0d d507 5a a8 06 5c 2800 ea08 2100 88
        is_power = 1;
        // type      = (b[5]);
        // unknown_1 = (b[6]);
        current = (b[8] << 8) | b[7];
        voltage = (b[10] << 8) | b[9];
        power   = (b[12] << 8) | b[11];
    }
    else if (msg_len == 14) {
        // energy 0x0e
        is_energy  = 1;
        // type       = (b[5]);
        energy_kWh = (b[8] << 16) | (b[7] << 8) | b[6];
        // tsec       = (b[10] << 8 | b[9]);
        // energy_kwh_l = (b[13] << 16) | (b[12] << 8) | b[11];
    }
    else if (msg_len == 17) {
        // 0x11 power at coldstart = initial power
        is_power = 1;
        // type      = (b[9]);
        current   = (b[12] << 8) | b[11];
        voltage   = (b[14] << 8) | b[13];
        power     = (b[16] << 8) | b[15];
    }
    else if (msg_len == 18) {
        // 0x12 energy at coldstart / initial energy
        is_energy  = 1;
        // type       = (b[9]);
        energy_kWh = (b[12] << 16) | (b[11] << 8) | b[10];
        // tsec =  (b[14] << 8 | b[13]);
        // tsec is FAULTY and useless by design, should be: "time since coldstart" in seconds or even better in minutes for 24bit
    }
    else {
        decoder_log_bitrow(decoder, 1, __func__, b, len, "unhandled message");
        return DECODE_FAIL_OTHER; // unhandled message
    }

    // calculation for PF (Powerfactor) is invalid if current is < 0.02 A
    // e.g. a standby device will show bad readings
    // double va = current * voltage * 0.001; // computed value
    // double powerf = va > 1.0 ? power / va : 1.0; // computed value

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",                 DATA_STRING, "Revolt-ZX7717",
            "id",               "Device ID",        DATA_INT,  id,
            "version",          "Version",          DATA_INT,  version,
            "current_A",        "Current",          DATA_COND, is_power, DATA_FORMAT, "%.3f A", DATA_DOUBLE, current * 0.001,
            "voltage_V",        "Voltage",          DATA_COND, is_power, DATA_FORMAT, "%.1f V", DATA_DOUBLE, voltage * 0.1,
            "power_W",          "Power",            DATA_COND, is_power, DATA_FORMAT, "%.1f W", DATA_DOUBLE, power * 0.1,
            "energy_kWh",       "energy_kWh",       DATA_COND, is_energy, DATA_FORMAT, "%.2f kWh", DATA_DOUBLE, energy_kWh * 0.01,
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
        "version",
        "current_A",
        "voltage_V",
        "power_W",
        "energy_kWh",
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
