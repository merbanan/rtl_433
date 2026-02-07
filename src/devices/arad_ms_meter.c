/** @file
    Arad/Master Meter Dialog3G water utility meter.

    Copyright (C) 2022 avicarmeli

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Arad/Master Meter Dialog3G water utility meter.

FCC-Id: TKCET-733

See notes in https://45851052.fs1.hubspotusercontent-na1.net/hubfs/45851052/documents/files/Interpreter-II-Register_v0710.20F.pdf
and https://www.arad.co.il/wp-content/uploads/Dialog-3G-register-information-sheet_Eng-002.pdf

Programmable parameters:
- Meter User ID:
  A municipal Meter ID number of up to 5 digits (16 or 17 bits needed)
- Transponder No:
  Meter’s Dialog 3GTM transponder number of up to 12 digits (40 bits needed)
- Reading:
  The transmitted Dialog 3G TM meter reading (up to 9 digits), (30 bits needed)
  the accumulated and the display readout are always equivalent.
- Meter Type:
  Meter type such as water, gas or electricity
- Count Factor:
  Meter count unit. It is a pre scale factor which is initially programmed
  into the Dialog 3G TM unit is order to get the standaed measurement units
  for the system billing, management and calculation (Gallons or Cubic/ Mettic)
- Alarms Temper:
  A warning temper sign, in case of unauthorized meter tampering. CCW: Reverse
  consumption by the meter.
- Gear Ratio:
  Water meter mechanical gear ratio parameter for the 3G Interpreter register types

Programmable registration includes USG, CF, or M3, while
resolution of the flow multiplier provides a custom-tailored
enhanced display (.01, 0.1, 1, 10, 100).

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

    UID:56h SERIAL: <24d c 8h COUNTER: <32d 8h8h 8h8h 8h8h  SUFFIX:hh

*/

static int arad_mm_dialog3g_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0x96, 0xf5, 0x13, 0x85, 0x37, 0xb4}; // 48 bit preamble

    int row = bitbuffer_find_repeated_row(bitbuffer, 1, 168); // expected 1 row with minimum of 48+120= 168 bits.
    if (row < 0) {
        return DECODE_ABORT_EARLY;
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, preamble_pattern, 48);
    start_pos += 48; // skip preamble

    if (start_pos + 120 > bitbuffer->bits_per_row[row]) {
        return DECODE_ABORT_LENGTH; // short buffer or preamble not found
    }

    bitbuffer_invert(bitbuffer);

    uint8_t b[15];
    bitbuffer_extract_bytes(bitbuffer, row, start_pos, b, 120);

    int serno    = b[0] | (b[1] << 8) | (b[2] << 16); // 24 bit little endian Meter Serial number
    int wreadraw = b[5] | (b[6] << 8) | (b[7] << 16); // 24 bit little endian Meter water consumption reading
    float wread = wreadraw * 0.1f;

    char sernoout[12];
    sprintf(sernoout, "%08u-%02x", serno, b[3]);

    /* clang-format off */
    data_t *data = data_make(
            "model",       "",               DATA_STRING,    "AradMsMeter-Dialog3G",
            "id",          "Serial No",      DATA_STRING,    sernoout,
            "volume_m3",   "Volume",         DATA_FORMAT,    "%.1f m3",  DATA_DOUBLE, wread,
            //"mic",         "Integrity",      DATA_STRING,    "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "volume_m3",
        //"mic",
        NULL,
};

r_device const arad_ms_meter = {
        .name        = "Arad/Master Meter Dialog3G water utility meter",
        .modulation  = FSK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 8.4,
        .long_width  = 8.4, // not used
        .reset_limit = 30,
        .decode_fn   = &arad_mm_dialog3g_decode,
        .disabled    = 1, // checksum not implemented
        .fields      = output_fields,
};
