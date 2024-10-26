/** @file
    Risco 2 way Agility protocol.

    Copyright (C) 2024 Bruno OCTAU (ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int risco_agility_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Risco 2 way Agility protocol.

Manufacturer :
- Risco Ltd.

Reference:
- Risco PIR RWX95PA Agility sensor,

FCC extract:
- The module is a transceiver which consist of a small PCB with an integral helical antenna,
which operates in the frequency of 433.92MHz Modulation is On-Off Keying using Manchester code with max bit rate of 2400Bps.
This module is installed only in RISCO 2-way wireless units, and it's behavior is determined by the host unit, as tested by ITL.
- Being bi-directional enables the detectors to receive an acknowledgment from the panel for every transmission.

This module, p/n RWRT433R000A, is a 433.92Mhz 2-way wireless module manufactured by RISCO Ltd.
The model consists of a small PCB, a header for connection to the host unit, and a helical integral antenna.
This model is not sold separately, and is not installed in any units other then RISCO 2-way wireless units, and currently it is used
in the following hosts:
- Agility Security panel       P/N: RW132x4t0zzA
- 2-Way I/O Expander           P/N: RW132I04000H
- 2-Way Wireless PIR Detector  P/N: RWX95043300A
- 2-Way Wireless PET Detector  P/N: RWX95P43300A

S.a. issue #3062

Data Layout:
- 2 types of message have been identified.
- 16 bytes
- or 33 bytes

Preamble/Syncword  .... : 0x555a

Short 16 bytes message:
                   0  8  16 24 34 40 48 56 64 72 80 88 96104112120
    Byte Position   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    Sample         ff 60 01 e1 9c b6 01 74 fe 28 0c 60 60 00 50 be
                   AA AA BB BC DD DD EE EE EE FF FF GG HI JJ ZZ ZZ

- AA:{16} flag 1, fixed 0xFF60
- BB:{12} flag 2, fixed 0x01E
- C: {4}  0 or 1 flag 3
- D: {16} Counter, 8 bits reversed and reflected binary coded, one bit change between message, each byte increases to maximum then decreases.
- EE:{24} Possible ID, not yet decoded from Wxxxxxxxxxxx number on the QR sticker.
- FF:{16} Fixed 0x280c value
- GG:{8}  flag 4, 0x60 from PIR sensor, 0xA0 from other type frame
- H: {4}  Alarm state, 0x6 (0x4 Gray decoded) = Tampered, 0xA (0x6) = Tampered_motion, 0xC (0x2) = Motion, 0x0 = Clear, not detection.
- I: {4}  0x0 = Normal, 0x3 (0x8) = Low Bat ?
- J: {4}  0 or 1
- ZZ:{16} CRC-16, poly 0x8005, init 0x8181

Bitbench:

    ? 16h ? 12h ZERO_OR_ONE 4h COUNTER ? 4h CMD ? 4h ID ? 32h FIX 16h ? 8h ? 8h ZERO_OR_ONE 8h CRC 16h 8h

Long 33 bytes message: (draft, to be reviewed)

    Byte Position   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32
    Sample         fe f8 01 d1 ba 18 01 ac 89 28 0c a0 03 01 e0 a3 19 01 06 00 00 c0 c0 00 df 3e 2f a5 f4 1e 00 82 1b
                   AA AA BB BC DE FF FF FF FF GG GG HH II JJ J? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ZZ ZZ

- AA:{16} flag 1, fixed 0xFEF8
- BB:{12} flag 2, fixed 0x01D
- C: {4} 0 or 1 flag 3
- D: {4} Counter ?
- E: {4} Command ? (send / acknowledge ?)
- FF:{32} Possible ID, not yet decoded from Wxxxxxxxxxxx number on the QR sticker.
- GG:{16} Fixed 0x280c value
- HH:{8} flag 4, 0x60 from PIR sensor, 0xA0 from other type frame
- II:{8} flag 5, 0x60 = Tampered, 0xA0 = Tampered_motion, 0xC0 = Motion, 0x03 from other type frame.
- ??: Unknown
- JJ:{12} flag 6, fixed 0x01E
- ZZ:{16} CRC-16, poly 0x8005, init 0x8181

Bitbench:

    ? 16h ? 12h ZERO_OR_ONE 4h ? COUNTER ? 4h CMD ? 4h ID ? 32h FIX 16h ? 8h 8h 12h 12h 32h 8h 72h ? 8h CRC 16h

*/

static int gray_decode(int n) {
    int p = n;
    while (n >>= 1) p ^= n;
    return p;
}

static int risco_agility_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitbuffer_t decoded = { 0 };
    uint8_t *b;
    uint8_t const preamble_pattern[] = {0x55, 0x5a};
    uint8_t len_msg = 16; // default for sensor message, could be 33 bytes for other Agility message not yet decoded

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int pos = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);
    if (pos >= bitbuffer->bits_per_row[0]) {
        decoder_logf(decoder, 1, __func__, "Preamble not found");
        return DECODE_ABORT_EARLY;
    }

    decoder_log_bitrow(decoder, 1, __func__, bitbuffer->bb[0], bitbuffer->bits_per_row[0], "MSG");

    bitbuffer_differential_manchester_decode(bitbuffer, 0, pos + sizeof(preamble_pattern) * 8, &decoded, len_msg * 8);

    decoder_log_bitrow(decoder, 1, __func__, decoded.bb[0], decoded.bits_per_row[0], "DMC");

    // check msg length

    if (decoded.bits_per_row[0] < len_msg * 8) {
        decoder_logf(decoder, 1, __func__, "Too short");
        return DECODE_ABORT_LENGTH;
    }

    b = decoded.bb[0];

    // verify checksum
    if (crc16(b, len_msg, 0x8005, 0x8181)) {
        decoder_logf(decoder, 1, __func__, "crc error");
        return DECODE_FAIL_MIC; // crc mismatch
    }

    // expected 0xFF60 short message, 0xFEF8 message not yet decoded properly
    int message_type = (b[0] << 8)| b[1];
    if ( message_type != 0xFF60) {
        decoder_logf(decoder, 1, __func__, "Wrong message type %04x", message_type);
        return DECODE_ABORT_LENGTH;
    }

    // ID is probably not well decoded as bit not reverse and not Gray decoded
    int id = (b[6] << 16) | (b [7] << 8) | b[8];

    reflect_bytes(b,16);

    // Alarm state, 0x4 = Tampered, 0x6 = Tampered_motion, 0x2 = Motion, 0x0 = Clear, not detection.
    int state        = gray_decode(b[12] & 0xF);
    int tamper       = (state & 0x4) >> 2;
    int motion       = (state & 0x2) >> 1;
    int low_batt     = (gray_decode((b[12] & 0xF0) >> 4) & 0x8)>> 3;
    int counter_raw  = (b[5] << 8) | b[4];
    int counter   = gray_decode(counter_raw);

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",                 DATA_STRING, "Risco-RWX95P",
            "id",          "",                 DATA_INT, id,
            "counter",     "Counter",          DATA_INT, counter,
            "tamper",      "Tamper",           DATA_COND,   tamper, DATA_INT, 1,
            "motion",      "Motion",           DATA_COND,   motion, DATA_INT, 1,
            "battery_ok",  "Battery_OK",       DATA_INT,    !low_batt,
            "mic",         "Integrity",        DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "counter",
        "tamper",
        "motion",
        "battery_ok",
        "mic",
        NULL,
};

r_device const risco_agility = {
        .name        = "Risco 2 Way Agility protocol, Risco PIR/PET Sensor RWX95P",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 175,
        .long_width  = 175,
        .reset_limit = 1000,
        .decode_fn   = &risco_agility_decode,
        .fields      = output_fields,
};
