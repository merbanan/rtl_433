/** @file
    Eberle Instat 868r1 floor heating thermostat remote (FSK, differential Manchester).

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Eberle Instat 868r1 floor heating thermostat remote (FSK, differential Manchester).

A 868 MHz 2-FSK transmitter used to learn, reset, and switch a receiver that
controls floor heating. Each button press sends 3 identical repeats.

Protocol reverse engineered from real captures and analysis in
https://github.com/merbanan/rtl_433/issues/1951 (readme.md, protocol.txt,
learn.txt/learn_Bitstream.txt and on_off.txt in the linked
dottoreD/rtl_433_tests fork), and cross-checked against 927 real "learn"
events and 36 real "on"/"off" events from that data.

Raw layout, 82 raw (chip) bits:

    30 bit fixed "beginning", then a 52 bit differential-Manchester-encoded
    "code part" that decodes (bitbuffer_differential_manchester_decode())
    to 25 bits: a fixed leading bit followed by 24 data bits (6 nibbles).

The 24 data bits are read two different ways depending on what they're used
for:

- Checksum: split into 6 nibbles of 4 raw (not further transformed) bits
  each, each nibble read LSB-first (i.e. the first-received bit of the
  nibble is its lowest-order bit). The sum of all 6 such nibbles is always
  0xb (mod 16), across all 963 real examples used to verify this.
- ID/action/data: the same 24 bits, Gray-decoded (cumulative XOR from the
  first bit) and then complemented, then split into 6 nibbles MSB-first:

      III III III AAAA DDDD ____
      (nibble 1-3: 12 bit ID, nibble 4: action code, nibble 5: action data,
      nibble 6: checksum nibble, already used above)

  The action code (nibble 4) depends on the parity of the ID:

      action  ID odd  ID even
      Learn      0x3     0xc
      Reset      0xb     0x4
      On         0xe     0x1
      Off        0x5     0xa

  For Learn/Reset, nibble 5 is a fixed parity marker (0xa odd / 0x5 even).
  For On/Off, nibble 5 is a 16 step rolling code, decrementing by one each
  time the button is pressed (independently for On and Off).

  The full action code table (including Reset and the ID-even On/Off codes)
  is independently confirmed by a second, unrelated reverse engineering
  effort: crasu's GNU Radio based decoder, https://github.com/crasu/gnu-heating
  (see parser/parser.py's cmd_dict, converting its binary keys to hex gives
  the exact same table). The Learn action and the ID-odd On/Off rolling
  code are additionally confirmed against real data here (927 Learn events,
  36 On/Off events).

There's also a longer Reset sequence (Reset#2-#9, 8 further repeats cycling
through additional codes) that is not understood and not decoded here.

Modulation timing (short_width/long_width) is 400 us, taken from crasu's
GNU Radio flowgraph (packet_decoder.py in the repo above): it resamples a
2 Msps capture down to 12500 Hz (decimation=160) and then averages every 5
of those into one output bit (gr-bitslice's slicer(omega=5)), i.e. 2500
bps. This is consistent with a ~30 ms elevated-power window found in one
of dottoreD's real VN0101 captures while analyzing it directly in the
frequency domain (82 bits * 400 us = 32.8 ms for one repeat) -- rtl_433's
own OOK-oriented pulse analyzer could not lock onto that capture at all
(its envelope is constant, being FSK, so there's no amplitude edge for the
analyzer's squelch to trigger on), so this has not been confirmed
end-to-end against a live receiver or raw capture with this decoder.
reset_limit is a guess pending real hardware/capture confirmation.
*/

/*
Device had not been found fully working for different Eberle Instat Revisions.
Tests have been done with following systems:analog VN0402, digital Rev2.6, digital Rev2.8 
- Pulse lenght changed from 400 => 360
- Gray code fully removed
- inversed and LSB nibble encoding
- nibble sequence for ID modified
- commands based on action codes modified for new encoding

Confirmed output using rtl_433 -f 868.9M -Y classic -M level:

time      : 2026-07-22 19:06:00
model     : Eberle-Instat868r1                     id        : 26C
Command   : Off          Action Code: 0            Data      : 5             Integrity : CHECKSUM
Modulation: FSK          Freq1     : 868.9 MHz     Freq2     : 868.8 MHz
RSSI      : -7.4 dB      SNR       : 20.6 dB       Noise     : -28.0 dB
*/

// Preamble: no leading zeros to allow leveling, but 18-bit sync marker.
#define PREAMBLE_BITS 18
static uint8_t const preamble_pattern[3] = {
    0xfe,   // 11111110
    0x03,   // 00000011
    0x00    // letzte 2 Bits = 00
};

static int nibble_lsb_first(uint8_t const *bitrow, unsigned bit_offset)
{
    int val = 0;
    for (int i = 0; i < 4; i++) {
        if (bitrow_get_bit(bitrow, bit_offset + i))
            val |= (1 << i);
    }
    return val;
}

// Read a 4-bit nibble LSB-first from the inverted raw bits.
// The transmitted raw bits are active-low, so every bit is inverted before
// the logical value is formed. Inverting then reading LSB-first is the same
// as reading LSB-first and complementing the nibble: val ^ 0xf.
static int nibble_lsb_first_inverted(uint8_t const *bitrow, unsigned bit_offset)
{
    return nibble_lsb_first(bitrow, bit_offset) ^ 0x0f;
}

static int eberle_instat868r1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row = 0; // we expect a single row only
    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[row] < 30 + 50) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned row_len      = bitbuffer->bits_per_row[row];
    unsigned search_start = 0;
    bitbuffer_t decoded   = {0};
    int mic_ok            = 0;
    while (search_start + PREAMBLE_BITS + 50 <= row_len) {
        unsigned pos = bitbuffer_search(bitbuffer, row, search_start, preamble_pattern, PREAMBLE_BITS);
        if (pos + PREAMBLE_BITS + 50 > row_len) {
            break;
        }

        bitbuffer_differential_manchester_decode(bitbuffer, row, pos + PREAMBLE_BITS, &decoded, 26);

        if (decoded.bits_per_row[0] < 25) {
            search_start = pos + PREAMBLE_BITS + 1; // look for a further repeat past this preamble
            continue;
        }
        uint8_t const *b = decoded.bb[0];

        // Checksum: sum of the 6 data nibbles, each read LSB-first, is always 0xb (mod 16).
        int checksum = 0;
        for (int n = 0; n < 6; n++) {
            checksum += nibble_lsb_first(b, 1 + n * 4);
        }
        if ((checksum & 0xf) != 0xb) {
            search_start = pos + PREAMBLE_BITS + 1; // look for a further repeat past this preamble
            continue;
        }

        mic_ok = 1;
        break; // only decode the first bitstream that validates the checksum
    }
    if (!mic_ok) {
        return DECODE_FAIL_MIC;
    }
    uint8_t const *b = decoded.bb[0];

    int nibble[6];
    for (int n = 0; n < 6; n++) {
        nibble[n] = nibble_lsb_first_inverted(b, 1 + n * 4);
    }

    int id     = (nibble[2] << 8) | (nibble[1] << 4) | nibble[0];
    int action = nibble[3];
    int data   = nibble[4];

    char const *command = "Unknown";
    if (action == 0x8 || action == 0xa)
        command = "Learn";
    else if (action == 0x9)
        command = "Reset";
    else if (action == 0x1 || action == 0x7 || action == 0x5)
        command = "On";
    else if (action == 0x0 || action == 0x4)
        command = "Off";

    /* clang-format off */
    data_t *data_out = data_make(
            "model",        "",             DATA_STRING, "Eberle-Instat868r1",
            "id",           "",             DATA_FORMAT, "%03x", DATA_INT, id,
            "command",      "Command",      DATA_STRING, command,
            "action_code",  "Action Code",  DATA_FORMAT, "%01x", DATA_INT, action,
            "data",         "Data",         DATA_FORMAT, "%01x", DATA_INT, data,
            "mic",          "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data_out);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "command",
        "action_code",
        "data",
        "mic",
        NULL,
};

r_device const eberle_instat868r1 = {
        .name        = "Eberle Instat 868r1 floor heating thermostat remote",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 360,
        .long_width  = 360,
        .reset_limit = 8000,
        .decode_fn   = &eberle_instat868r1_decode,
        .fields      = output_fields,
};
