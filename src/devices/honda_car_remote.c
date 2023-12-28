/** @file
    Handa - Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int honda_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Honda - Car Remote (315 MHz)

Manufacturer:
- Honda

Supported Models:
- OUCG8D-344H-A

Data structure:

The transmitter uses a rolling code message.

Button operation:
The unlock, lock buttons can be pressed once to transmit a single message.
The trunk, panic buttons will transmit the same code on a short press.
The trunk, panic buttons will transmit the unique code on a long press.
The panic button will repeat the panic code as long as it is held.

Data layout:

Bytes are inverted.

Example:
codes     : {385}fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff0ee6f22beaeaa7d0

The decoder will match on the last 64 bits of the preamble: 0xfffffff0

SSSS IIIII bbbb CC

- I: 16 bit sequence that increments on each code transmitted
- I: 20 bit remote ID
- b: b bit button code
- C: 8 bit unknown code, possibly a checksum or rolling code

Format string:

SEQUENCE hhhh ID: hhhhhhh BUTTON: bbbb CODE: bbbbbbbb

*/

#include <decoder.h>

static int honda_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] < 128) {
        return DECODE_ABORT_LENGTH;
    }

    uint8_t pattern[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0};
    int offset         = bitbuffer_search(bitbuffer, 0, 0, pattern, 64) + 64;

    if (bitbuffer->bits_per_row[0] - offset < 56) {
        return DECODE_ABORT_EARLY;
    }

    bitbuffer_invert(bitbuffer);

    uint8_t bytes[8];
    bitbuffer_extract_bytes(bitbuffer, 0, offset, bytes, 64);

    int id       = (bytes[2] << 20) | (bytes[3] << 12) | (bytes[4] << 4) | (bytes[5] >> 4);
    int sequence = (bytes[0] << 8) | bytes[1];
    int button   = bytes[5] & 0xf;
    int code     = bytes[6];

    int unlock = button == 3;
    int lock   = button == 4;
    int trunk  = button == 15;
    int panic  = button == 6;

    if (id == 0 ||
            button == 0 ||
            sequence == 0 ||
            id == 0xfffffff ||
            sequence == 0xffff) {
        return DECODE_FAIL_SANITY;
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",       "model",     DATA_STRING, "Honda-OUCG8D",
            "id",          "device-id", DATA_INT,    id,
            "sequence",    "Sequence",  DATA_INT,    sequence,
            "button_code", "Button",    DATA_INT,    button,
            "code",        "Code",      DATA_INT,    code,
            "lock",        "Lock",      DATA_INT,    lock,
            "unlock",      "Unlock",    DATA_INT,    unlock,
            "trunk",       "Trunk",     DATA_INT,    trunk,
            "panic",       "Panic",     DATA_INT,    panic,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "sequence",
        "button_code",
        "code",
        "lock",
        "unlock",
        "trunk",
        "panic",
        NULL,
};

r_device const honda_car_remote = {
        .name        = "Honda Car Remote (-f 313M -s 240k)",
        .modulation  = OOK_PULSE_PWM, // this is actually FSK, but I was not able to decode using that modulation. Tuning to one end of the signal works with OOK PWM modulation.
        .short_width = 242,
        .long_width  = 483,
        .reset_limit = 492,
        .decode_fn   = &honda_car_remote_decode,
        .fields      = output_fields,
};