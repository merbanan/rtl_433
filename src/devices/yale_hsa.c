/** @file
    Yale HSA (Home Security Alarm) protocol.

    Copyright (C) 2022 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Yale HSA (Home Security Alarm) protocol.

Yale HSA Alarms, YES-Alarmkit:
- Yale HSA6010 Door/Window Contact
- Yale HSA6080 Keypad
- Yale HSA6020 Motion PIR
- Yale HSA6060 Remote Keyfob

A message is made up of 6 packets and then repeats.
Packets are 13 bits, start with 0x5 and a end-of-message flag, then 8 bit data.

Actually data should be in the gaps, which are tighter timings of 368 / 978 us.

The 6 packets combined decode as

Data Layout:

    ID:16h TYPE:8h STATE:8b EVENT:8h CHK:8h

Or perhaps?

    ID:16h TYPE:12h STATE:8b EVENT:4h CHK:8h

The checksum is just remainder of adding the 5 messages bytes, i.e. adding 6 bytes checks to zero.

Guessed data so far:
- Sensor types: ac1, ad1: window sensor, 153: PIR
- Events 1: trigger, 3: binding, 4: tamper
- State: Could be battery?

Data table:
- W/D: Contact opened:              stype: ac state: 1 0 event: 01
- W/D: Tamper button closed/off:    stype: ac state: 1 0 event: 04
- W/D: Tamper button released/on:   stype: ac state: 1 2 event: 04
- W/D: Binding button pressed:      stype: ac state: 1 2 event: 03
- W/D: Low battery:                 stype: ac state: 1 8 event: 04
- PIR:  Binding Button:             stype: 15 state: 3 0 event: 03
- PIR:  Tamper button closed/off:   stype: 15 state: 3 0 event: 04
- PIR:  Tamper button released/on:  stype: 15 state: 3 2 event: 04
- PIR:  Movement trigger:           stype: 15 state: 3 0 event: 01
- PIR:  Low battery:                stype: 15 state: 3 2 event: 01

Get Raw data with:

    rtl_433 -R 0 -X 'n=name,m=OOK_PWM,s=850,l=1460,y=5380,r=1500' ~/Desktop/Yale-6010.ook

*/

static int yale_hsa_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // Require at least 6 rows
    if (bitbuffer->num_rows < 6)
        return DECODE_ABORT_EARLY;

    uint8_t msg[6] = {0};
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        // Find one full message
        int ok = 0;
        for (int i = 0; i < 6; ++i, ++row) {
            if (bitbuffer->bits_per_row[row] != 13)
                break; // wrong length
            uint8_t *b = bitbuffer->bb[row];
            if ((b[0] & 0xf0) != 0x50)
                break; // wrong sync
            int eom = (b[0] & 0x08);
            if ((i < 5 && eom) || (i == 5 && !eom))
                break; // wrong end-of-message
            bitbuffer_extract_bytes(bitbuffer, row, 5, &msg[i], 8);
            if (i == 5)
                ok = 1;
        }
        // Skip to end-of-message on error
        if (!ok) {
            for (; row < bitbuffer->num_rows; ++row) {
                uint8_t *b = bitbuffer->bb[row];
                int eom    = (b[0] & 0x08);
                if (eom)
                    break; // end-of-message
            }
            continue;
        }
        // Message found
        int chk = add_bytes(msg, 6);
        if (chk & 0xff)
            continue; // bad checksum

        // Get the data
        int id    = (msg[0] << 8) | (msg[1]);
        int stype = (msg[2]);
        int state = (msg[3]);
        int event = (msg[4]);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "Yale-HSA",
                "id",           "",             DATA_FORMAT, "%04x", DATA_INT, id,
                "stype",        "Sensor type",  DATA_FORMAT, "%02x", DATA_INT, stype,
                "state",        "State",        DATA_FORMAT, "%02x", DATA_INT, state,
                "event",        "Event",        DATA_FORMAT, "%02x", DATA_INT, event,
                "mic",          "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "stype",
        "state",
        "event",
        "mic",
        NULL,
};

r_device const yale_hsa = {
        .name        = "Yale HSA (Home Security Alarm), YES-Alarmkit",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 850,
        .long_width  = 1460,
        .sync_width  = 5380,
        .reset_limit = 1500,
        .decode_fn   = &yale_hsa_decode,
        .fields      = output_fields,
};
