/** @file
    Code Alarm - FRDPC2002 Car Remote.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn int code_alarm_frdpc2000_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Code Alarm - Car Remote

Manufacturer:
- Code Alarm

Supported Models:
- FRDPC2002, GOH-FRDPC2002

Data structure:

This transmitter uses a rolling code.
The same code is continuously repeated while button is held down.
Multiple buttons can be pressed to set multiple button flags.

Data layout:

PPPP uuuu bbbb IIIIIIII uuuu

- P: 32 bit Preamble, all 0x00
- u: 4 bit unknown
- b: 4 bit button flags
- I: 24 bit ID (This is 32 bits raw, and each byte is XOR'd to form a 24 bit ID)
- u: 4 bit unknown

Format string:

PREAMBLE: hhhh UNKNOWN: bbbb BUTTON: bbbb ID: hhhhhhhh bbbbbbbb

*/

#include "decoder.h"

static int code_alarm_frdpc2000_car_remote_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] != 60) {
        return DECODE_ABORT_LENGTH;
    }

    if (bitbuffer->bb[0][0] != 0x00 || bitbuffer->bb[0][1] != 0x00) {
        return DECODE_FAIL_SANITY;
    }

    uint8_t bytes[5];
    bitbuffer_extract_bytes(bitbuffer, 0, 19, bytes, 40);

    int bytes_sum = add_bytes(bytes, 5);
    if (bytes_sum == 0 || bytes_sum >= 0xff * 5) {
        return DECODE_FAIL_SANITY;
    }

    uint8_t code[5];
    bitbuffer_extract_bytes(bitbuffer, 0, 23, code, 36); // manual tied to FCC id states a 36bit rolling code

    char data_str[11];
    snprintf(data_str, sizeof(data_str), "%02X%02X%02X%02X%02X", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);

    uint32_t id = ((code[0] ^ code[1]) << 16) | ((code[1] ^ code[2]) << 8) | (code[2] ^ code[3]);
    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%06X", id);

    // parse button
    int button          = bytes[0] >> 4;
    char button_str[64] = "";

    typedef struct {
        const char *name;
        const uint8_t len;
        const uint8_t *vals;
    } Button;

    /* clang-format off */
    const Button button_map[5] = {
        { .name = "Multiple", .len = 1,  .vals = (uint8_t[]){ 0x7 } },
        { .name = "Lock",     .len = 2,  .vals = (uint8_t[]){ 0x6, 0x4 } },
        { .name = "Panic",    .len = 2,  .vals = (uint8_t[]){ 0x1, 0x3 } },
        { .name = "Start",    .len = 2,  .vals = (uint8_t[]){ 0x0, 0x3 } },
        { .name = "Unlock",   .len = 2,  .vals = (uint8_t[]){ 0x5, 0x4 } },
    };
    /* clang-format on */
    const char *delimiter = "; ";
    const char *unknown   = "?";

    int matches = 0;
    // iterate over the button-to-value map to record which button(s) are pressed
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < button_map[i].len; j++) {
            if (button == button_map[i].vals[j]) { // if the button values matches the value in the map
                if (matches) {
                    strcat(button_str, delimiter); // append a delimiter if there are multiple buttons matching
                }
                strcat(button_str, button_map[i].name); // append the button name
                matches++;                              // record the match
                break;                                  // move to the next button
            }
        }
    }

    if (!matches) {
        strcat(button_str, unknown);
    }

    /* clang-format off */
    data_t *data = data_make(
            "model",       "model",       DATA_STRING, "CodeAlarm-FRDPC2002",
            "id",          "ID",          DATA_STRING, id_str,
            "button_code", "Button Code", DATA_INT,    button,
            "button_str",  "Button",      DATA_STRING, button_str,
            "data",        "Data",        DATA_STRING, data_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "button_code",
        "button_str",
        NULL,
};

r_device const code_alarm_frdpc2000_car_remote = {
        .name        = "Code Alarm FRDPC2002 Car Remote",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 550,
        .long_width  = 1100,
        .reset_limit = 1600,
        .tolerance   = 100,
        .decode_fn   = &code_alarm_frdpc2000_car_remote_decode,
        .fields      = output_fields,
};