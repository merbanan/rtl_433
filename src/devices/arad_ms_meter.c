/** @file
    Arad/Master Meter Dialog3G water utility meter.

    Copyright (C) 2022 avicarmeli
    modified 2025 Boing <dhs.mobil@gmail.com<

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Arad/Master Meter Dialog3G water utility meter.

FCC-Id: TKCET-733

Message is being sent once every 30 seconds.
The message looks like:

    00000000FFFFFFFFFFFFFFSSSSSSSSXXCCCCCCXXXF?????????XFF

where:

- 00000000 is preamble.
- FFFFFFFFFFFFFF  is fixed in time and the same for other meters in the neighborhood. Probably gearing ratio. The payload is 3e690aec7ac84b.
- SSSSSSSS  is Meter serial number.  for instance fa1c9073 =>  fa1c90 = 09444602, little endian 73= 'S'
- XX no idea.
- CCCCCC is the counter reading little endian for instance a80600= 1704
- XXX no idea.
- F  is fixed in time and the same for other meters in the neighborhood. With payload of 5.
- ????????? probably some kind of CRC or checksum - here is where I need help.
- X is getting either 8 or 0 same for other meters in the neighborhood.
- FF is fixed in time and the same for other meters in the neighborhood.With payload f8.

Format string:

    56x SERIAL: <24dc 8x COUNTER: <24d hhhhhhhhhhhhhh  SUFFIX:hh

Modification by Boing:
- Arad Master Meter Dialog3G
  see: https://45851052.fs1.hubspotusercontent-na1.net/hubfs/45851052/documents/files/Interpreter-II-Register_v0710.20F.pdf
  https://www.arad.co.il/wp-content/uploads/Dialog-3G-register-information-sheet_Eng-002.pdf
  https://www.arad.co.il/wp-content/uploads/Sonata-Pulse-output-GR.pdf

- 6 values send

- AMR/AMI
- Remote Small Concentrator
- Dialog3G™
- Meter User ID    up to 5 digits

Transponder No Meter’s Dialog 3GTM transponder number of up to 12 digits
Reading The transmitted Dialog 3G TM meter reading (up to 9 digits), the accumulated and the display
readout are always equivalent.

type such as water, gas, electricity or other
Count Factor Meter count unit. It is a pre scale factor which
is initially programmed into the Dialog 3G TM
unit is order to get the standaed measurement
units for the system billing, management and
calculation (Gallons or Cubic/ Mettic)
Alarms Temper: A warning temper sign, in case of
unauthorized meter tampering.
CCW: Reverse consumption by the meter.
Gear Ratio Water meter mechanical gear ratio parameter
for the 3G Interpreter register types
Dialog 3G operatates on the ISM band 900-916Mhz, (illegal in Germany !- military use)
with 3 modes and 3 bandwidths (up to 920khz)

*/

static int arad_mm_dialog3g_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Dialog 3G default parameter (fully programmable, so values may change)
    //invert: no
    //RAW message at least 184 bit  ({184}07cd215d8f590968fee5c0080be4a0000193d8b96aa71f)

    //payload unaligned:    f9a42bb1eb212d1fdcb801034090000030ba8783bdc3e0

    //sync word: 24bit {0x3e, 0x69, 0x0a}
    //Data length: 0x17  (23 Byte)
    //valid payload has 23 byte,  at least{184}07cd215d8f5909696a7bce0a0263c5600189b16895151f
    //RAW code
    //{228}a5e0000000007cd215d8f590968fee5c0080be4a0000193d8b96aa71f
    //payload:    3e690aec7ac84b 47f72e 0040 5f2500 000c 9ec5cb5538 f8

    //    000: 3e 69 0a ec 7a c8 4b     UID  unified transponder  ID
    //    007: 47 f7 2e                 <24d    Serial NO    (03077959)
    //    010: 00 40                    ? format
    //    012: 5f 25 00                 <24d    Volume    (956.7 m3)
    //    015: 00 0c                    ? format
    //    017: 9e c5 cb 55 38           ?
    //    022: f8                       Suffix    (f8)

    int row = bitbuffer_find_repeated_row(bitbuffer, 1, 184);
    if (row < 0) {
        decoder_logf(decoder, 1, __func__, "expected 1 row with at least 184 bits, row:%3d", row);
        return DECODE_ABORT_EARLY; // expected 1 row with at least 184 bits.
    }

    uint8_t bits_per_row = bitbuffer->bits_per_row[row];
    if (bits_per_row > 232) {
        decoder_logf(decoder, 1, __func__, "row(%2d), > MAX 232 bits (%3d)", row, bits_per_row);
        return DECODE_ABORT_EARLY;
    }
    if (bits_per_row < 184) {
        decoder_logf(decoder, 1, __func__, "row(%2d), < MIN 184 bits (%3d)", row, bits_per_row);
        return DECODE_ABORT_EARLY;
    }

    // check Default TID
    // autoinvert
    //uint8_t const def_pattern[3] = {0x3e, 0x69, 0x0a}; // MSB is mostly broken due to .short_width = 8.4
    uint8_t const def_pattern[3] = {0x69, 0x0a, 0xec};
    int syncpos = bitbuffer_search(bitbuffer, row, 0, def_pattern, 24);

    syncpos -= 8;
    int is_inverted = 0;
    if (syncpos > 56) {
        bitbuffer_invert(bitbuffer);
        is_inverted = 1;
        syncpos     = bitbuffer_search(bitbuffer, row, 0, def_pattern, 24);
        if (syncpos > 56) {
            decoder_log(decoder, 1, __func__, "Sync Not found");
            decoder_log(decoder, 1, __func__, "maybe modified by provider");
            return DECODE_ABORT_EARLY;
        }
    }

    uint8_t b[26];
    bitbuffer_extract_bytes(bitbuffer, row, syncpos, b, 184);

    // Length check
    if ((bits_per_row - syncpos) < 176) { //we need at least 176 bits
        decoder_logf(decoder, 1, __func__, "Length check failed (%3d)", bits_per_row - syncpos);
        return DECODE_ABORT_LENGTH;
    }

    // find the unique suffix 0xf8 // optional
    uint8_t const suffix_pattern[] = {0xf8};
    uint8_t suffix_pos = bitbuffer_search(bitbuffer, row, bits_per_row-10, suffix_pattern, 5);
    if (suffix_pos > bits_per_row - 5) { // match near end of message
        decoder_logf(decoder, 1, __func__, "Suffix not found (%4d)", suffix_pos);
        // return DECODE_ABORT_LENGTH; // Suffix not found
    }

    // get the Transponder ID // valid for this UID meter group only
    int i = 0;
    char UID[16]; // Unified Transponder ID
    for (i = 0; i < 7; ++i) {
        sprintf(&UID[i * 2], "%02x", b[i]);
    }
    // get the meter serial number
    int serno    = b[7] | (b[8] << 8) | (b[9] << 16); // 24 bit little endian Meter Serial number
    // get the RAW water consumption
    int wreadraw = b[12] | (b[13] << 8) | (b[14] << 16); // 24 bit little endian Meter water consumption reading
    float wread = wreadraw * 0.1f;
    // get the payload for further debug
    char payload[52];
    for (i = 0; i < 23; ++i) {
        sprintf(&payload[i * 2], "%02x", b[i]);
    }

    if (is_inverted) {
        bitbuffer_invert(bitbuffer); // reverse inverted for debug issue
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",               DATA_STRING,    "AradMsMeter-Dialog3G",
            "UID",         "UID",            DATA_STRING,    UID,
            "id",          "Serial",         DATA_INT,       serno,
            "volume_m3",   "Volume",         DATA_FORMAT,    "%.1f m3",  DATA_DOUBLE, wread,
            //"mic",         "Integrity",      DATA_STRING,    "CHECKSUM",
            "payload",     "Payload",        DATA_STRING,    payload,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "UID",
        "id",
        "waterread",
        "payload",
        //"mic",
        NULL,
};

r_device const arad_ms_meter = {
        .name        = "Arad/Master Meter Dialog3G water utility meter",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 8.4,
        .long_width  = 8.4,
        .reset_limit = 100,
        .decode_fn   = &arad_mm_dialog3g_decode,
        .disabled    = 1, // checksum not implemented
        .fields      = output_fields,
};
