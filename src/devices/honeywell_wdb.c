/* Honeywell wireless door bell, PIR Motion sensor
 *
 * Copyright (C) 2018 Benjamin Larsson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/** Frame documentation courtesy of https://github.com/klohner/honeywell-wireless-doorbell
 *
 * # Frame bits used in Honeywell RCWL300A, RCWL330A, Series 3, 5, 9 and all Decor Series
 * Wireless Chimes
 * # 0000 0000 1111 1111 2222 2222 3333 3333 4444 4444 5555 5555
 * # 7654 3210 7654 3210 7654 3210 7654 3210 7654 3210 7654 3210
 * # XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XX.. XXX. .... KEY DATA (any change and receiver doesn't seem to
 * #                                                                       recognize signal)
 * # XXXX XXXX XXXX XXXX XXXX .... .... .... .... .... .... .... KEY ID (different for each transmitter)
 * # .... .... .... .... .... 0000 00.. 0000 0000 00.. 000. .... KEY UNKNOWN 0 (always 0 in devices I've tested)
 * # .... .... .... .... .... .... ..XX .... .... .... .... .... DEVICE TYPE (10 = doorbell, 01 = PIR Motion sensor)
 * # .... .... .... .... .... .... .... .... .... ..XX ...X XXX. FLAG DATA (may be modified for possible effects on
 * #                                                                        receiver)
 * # .... .... .... .... .... .... .... .... .... ..XX .... .... ALERT (00 = normal, 01 or 10 = right-left halo light
 * #                                                                    pattern, 11 = full volume alarm)
 * # .... .... .... .... .... .... .... .... .... .... ...X .... SECRET KNOCK (0 = default, 1 if doorbell is pressed 3x
 * #                                                                           rapidly)
 * # .... .... .... .... .... .... .... .... .... .... .... X... RELAY (1 if signal is a retransmission of a received
 * #                                                                    transmission, only some models)
 * # .... .... .... .... .... .... .... .... .... .... .... .X.. FLAG UNKNOWN (0 = default, but 1 is accepted and I don't
 * #                                                                           oberserve any effects)
 * # .... .... .... .... .... .... .... .... .... .... .... ..X. LOWBAT (1 if battery is low, receiver gives low battery
 * #                                                                     alert)
 * # .... .... .... .... .... .... .... .... .... .... .... ...X PARITY (LSB of count of set bits in previous 47 bits)
*/

#include "decoder.h"

static int honeywell_wdb_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    int row, secret_knock, relay, battery, parity;
    uint8_t *bytes;
    data_t *data;
    unsigned int device, tmp;
    char *class, *alert;

    // The device transmits many rows, check for 12 matching rows.
    row = bitbuffer_find_repeated_row(bitbuffer, 4, 48);
    if (row < 0) {
        return 0;
    }
    bytes = bitbuffer->bb[row];


    if (bitbuffer->bits_per_row[row] != 48)
        return 0;

    bitbuffer_invert(bitbuffer);

    /* Parity check (must be EVEN) */
    parity = parity_bytes(bytes, 6);

    if (parity) { // ODD parity detected
        if (decoder->verbose > 1) {
            bitbuffer_print(bitbuffer);
            fprintf(stderr, "honeywell_wdb: Parity check on row %d failed (%d)\n", row, parity);
        }
        return 0;
    }

    device = bytes[0] << 12 | bytes[1] << 4 | (bytes[2]&0xF);
    tmp = (bytes[3]&0x30) >> 4;
    switch (tmp) {
        case 0x1: class = "PIR Motion sensor"; break;
        case 0x2: class = "Doorbell"; break;
        default:  class = "Unknown"; break;
    }
    tmp = bytes[4]&0x3;
    switch (tmp) {
        case 0x0: alert = "Normal"; break;
        case 0x1:
        case 0x2: alert = "High"; break;
        case 0x3: alert = "Full"; break;
        default:  alert = "Unknown"; break;
    }
    secret_knock = (bytes[5]&0x10) >> 4;
    relay = (bytes[5]&0x8) >> 3;
    battery = (bytes[5]&0x2) >> 1;
    data = data_make(
            "model",         "",            DATA_STRING, _X("Honeywell-Security","Honeywell Wireless Doorbell"),
            "id",            "Id",          DATA_FORMAT, "%x",   DATA_INT,    device,
            "class",         "Class",       DATA_FORMAT, "%s",   DATA_STRING, class,
            "alert",         "Alert",       DATA_FORMAT, "%s",   DATA_STRING, alert,
            "secret_knock",  "Secret Knock",DATA_FORMAT, "%d",   DATA_INT,    secret_knock,
            "relay",         "Relay",       DATA_FORMAT, "%d",   DATA_INT,    relay,
            "battery",       "Battery",     DATA_STRING, battery ? "LOW" : "OK",
            "mic",           "Integrity",   DATA_STRING, "PARITY",
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}


static char *output_fields[] = {
    "model",
    "id",
    "class",
    "alert",
    "secret_knock",
    "relay",
    "battery",
    "mic",
    NULL
};

r_device honeywell_wdb = {
    .name          = "Honeywell Wireless Doorbell",
    .modulation    = OOK_PULSE_PWM,
    .short_width   = 175,
    .long_width    = 340,
    .gap_limit     = 0,
    .reset_limit   = 5000,
    .sync_width    = 500,
    .decode_fn     = &honeywell_wdb_callback,
    .disabled      = 0,
    .fields        = output_fields,
};

r_device honeywell_wdb_fsk = {
    .name          = "Honeywell Wireless Doorbell (FSK)",
    .modulation    = FSK_PULSE_PWM,
    .short_width   = 160,
    .long_width    = 320,
    .gap_limit     = 0,
    .reset_limit   = 560,
    .sync_width    = 500,
    .decode_fn     = &honeywell_wdb_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
