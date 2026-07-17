/** @file
    Nidec - Car Remote.

    Copyright (C) 2024 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Nidec - Car Remote (313 MHz).

Manufacturer:
- Nidec

Supported Models:
- OUCG8D-344H-A (OEM for Honda)

Data structure:

The transmitter uses a rolling code message.

Button operation:
The unlock, lock buttons can be pressed once to transmit a single message.
The trunk, panic buttons will transmit the same code on a short press.
The trunk, panic buttons will transmit the unique code on a long press.
The panic button will repeat the panic code as long as it is held.

Data layout:

Bytes are inverted.

The decoder will match on the last 64 bits of the preamble: 0xfffffff0

    SSSS IIIIII 5b CCCC

- S: 16 bit sequence (plain rolling counter) that increments on each code transmitted
- I: 24 bit remote ID
- 5: 4 bit constant 0x5, seen on every real remote/button so far -- could be a fixed
  part of the protocol, or the low nibble of a wider ID; not enough distinct remotes
  seen to tell, so it's kept out of the reported ID rather than guessed at
- b: 4 bit button code
- C: 16 bit security/rolling-code field, changes nonlinearly with the counter. Not a
  conventional checksum: exhaustive testing (CRC-8/16 over all standard polynomials,
  reflected/reversed, arbitrary init/final, every byte subset, plus additive/XOR/
  Fletcher variants) found no match, and a plain incrementing counter would be
  linear. Most likely a keyed rolling-code authenticator computed by the
  transmitter's MCU (NEC uPD789860 w/ EEPROM per the FCC filing), not derivable
  from the wire data alone.

On real captures this last field is almost always truncated: the demod cuts the
frame short before all 16 bits arrive, and older versions of this decoder papered
over that by always extracting a full 16 bits regardless, silently zero-padding
the missing tail bits and reporting them as if they were real. `security_bits`
reports how many of the 16 bits were actually captured -- treat anything beyond
that count in `security` as padding, not signal.

*/

static int nidec_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] < 128) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t pattern[8] = {0xff, 0xff, 0xff, 0xf0};
    int offset         = bitbuffer_search(bitbuffer, 0, 0, pattern, 32) + 32;

    if (bitbuffer->bits_per_row[0] - offset < 56) {
        return DECODE_ABORT_EARLY;
    }
    // bits actually captured for the trailing 16 bit security field, the rest is
    // bitbuffer_extract_bytes() zero-padding, not a real observation -- see below
    int security_bits = bitbuffer->bits_per_row[0] - offset - 48;
    if (security_bits > 16) {
        security_bits = 16;
    }

    bitbuffer_invert(bitbuffer);

    uint8_t bytes[8];
    bitbuffer_extract_bytes(bitbuffer, 0, offset, bytes, 64);

    int sequence      = (bytes[0] << 8) | bytes[1];
    uint32_t id       = (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 8 | bytes[4];
    int button        = bytes[5] & 0xf;
    uint16_t security = (uint16_t)bytes[6] << 8 | bytes[7];

    // upper nibble of byte 5 is constant 0x5 across every real remote/button seen so far
    if ((bytes[5] & 0xf0) != 0x50) {
        return DECODE_FAIL_SANITY;
    }

    if (id == 0 ||
            sequence == 0 ||
            id == 0xffffff ||
            sequence == 0xffff ||
            security == 0 ||
            security == 0xffff) {
        return DECODE_FAIL_SANITY;
    }

    // only these 5 button codes have ever been observed on real remotes
    if (button != 0x3 && button != 0x4 && button != 0x5 && button != 0x6 && button != 0xf) {
        return DECODE_FAIL_SANITY;
    }

    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%06X", id);

    // only security_bits of this are an actual observation, the rest is
    // bitbuffer_extract_bytes() zero-padding past the end of a short capture
    char security_str[5];
    snprintf(security_str, sizeof(security_str), "%04X", security);

    char const *button_str;
    /* clang-format off */
    switch (button) {
        case 0x3: button_str = "Lock"; break;
        case 0x4: button_str = "Unlock"; break;
        case 0x5: button_str = "Trunk/Panic Short Press"; break;
        case 0x6: button_str = "Panic Long Press"; break;
        case 0xf: button_str = "Trunk Long Press"; break;
        default: button_str = "?"; break;
    }
    /* clang-format on */

    /* clang-format off */
    data_t *data = data_make(
            "model",         "model",         DATA_STRING, "Nidec-OUCG8D",
            "id",            "ID",            DATA_STRING, id_str,
            "security",      "",              DATA_STRING, security_str,
            "security_bits", "Security Bits", DATA_INT,    security_bits,
            "sequence",      "Sequence",      DATA_INT,    sequence,
            "button_code",   "Button Code",   DATA_INT,    button,
            "button_str",    "Button",        DATA_STRING, button_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "security",
        "security_bits",
        "sequence",
        "button_code",
        "button_str",
        NULL,
};

r_device const nidec_car_remote = {
        .name        = "Nidec Car Remote (-f 313.8M -s 1024k)",
        .modulation  = FSK_PULSE_PWM,
        .short_width = 250,
        .long_width  = 500,
        .reset_limit = 1000,
        .decode_fn   = &nidec_car_remote_decode,
        .fields      = output_fields,
        .disabled    = 1, // security field is almost always truncated by the demod (see security_bits) and isn't a verifiable checksum, so unwanted frames can't be ruled out
};
