/** @file
    inFactory outdoor temperature and humidity sensor.

    Copyright (C) 2017 Sirius Weiß <siriuz@gmx.net>
    Copyright (C) 2017 Christian W. Zuckschwerdt <zany@triq.net>
    Copyright (C) 2020 Hagen Patzke <hpatzke@gmx.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/** @fn int infactory_decode(r_device *decoder, bitbuffer_t *bitbuffer)
inFactory outdoor temperature and humidity sensor.

Outdoor sensor, transmits temperature and humidity data
- inFactory NC-3982-913/NX-5817-902, Pearl (for FWS-686 station)
- nor-tec 73383 (weather station + sensor), Schou Company AS, Denmark
- DAY 73365 (weather station + sensor), Schou Company AS, Denmark
- Tchibo NC-3982-675

Known brand names: inFactory, nor-tec, GreenBlue, DAY. Manufacturer in China.


Transmissions includes an id. Every 60 seconds the sensor transmits 6 packets:

    0000 1111 | 0011 0000 | 0101 1100 | 1110 0111 | 0110 0001
    iiii iiii | cccc ub?? | tttt tttt | tttt hhhh | hhhh ??nn

- i: identification // changes on battery switch
- c: CRC-4 // CCITT checksum, see below for computation specifics
- u: TX-button, also set for 3 sec at power-up
- b: battery low // flag to indicate low battery voltage
- h: Humidity // BCD-encoded, each nibble is one digit, 'A0' means 100%rH
- t: Temperature // in °F as binary number with one decimal place + 90 °F offset
- n: Channel // Channel number 1 - 3

Usage:

    # rtl_433 -f 434052000 -R 91 -F json:log.json
    # rtl_433 -R 91 -F json:log.json
    # rtl_433 -C si

Payload looks like this:

    [00] {40} 0f 30 5c e7 61 : 00001111 00110000 01011100 11100111 01100001

(See below for more information about the signal timing.)
*/

static int infactory_crc_check(uint8_t *b)
{
    uint8_t msg_crc, crc, msg[5];
    memcpy(msg, b, 5);
    msg_crc = msg[1] >> 4;
    // for CRC computation, channel bits are at the CRC position(!)
    msg[1] = (msg[1] & 0x0F) | (msg[4] & 0x0F) << 4;
    // crc4() only works with full bytes
    crc = crc4(msg, 4, 0x13, 0); // Koopmann 0x9, CCITT-4; FP-4; ITU-T G.704
    crc ^= msg[4] >> 4; // last nibble is only XORed
    return (crc == msg_crc);
}

static int infactory_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->bits_per_row[0] != 40)
        return DECODE_ABORT_LENGTH;

    uint8_t *b = bitbuffer->bb[0];

    // Check that the last 4 bits of message are not 0 (channel number 1 - 3)
    if (!(b[4] & 0x0F))
        return DECODE_ABORT_EARLY;

    if (!infactory_crc_check(b))
        return DECODE_FAIL_MIC;

    int id          = b[0];
    int button      = (b[1] >> 3) & 1;
    int battery_low = (b[1] >> 2) & 1;
    int temp_raw    = (b[2] << 4) | (b[3] >> 4);
    int humidity    = (b[3] & 0x0F) * 10 + (b[4] >> 4); // BCD, 'A0'=100%rH
    int channel     = b[4] & 0x03;

    float temp_f    = (temp_raw - 900) * 0.1f;

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "inFactory-TH",
            "id",               "ID",           DATA_INT,    id,
            "channel",          "Channel",      DATA_INT,    channel,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "button",           "Button",       DATA_INT,    button,
            "temperature_F",    "Temperature",  DATA_FORMAT, "%.2f F", DATA_DOUBLE, temp_f,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "button",
        "temperature_F",
        "humidity",
        "mic",
        NULL,
};

/*
Analysis using Genuino (see http://gitlab.com/hp-uno, e.g. uno_log_433):

Observed On-Off-Key (OOK) data pattern:

    preamble            syncPrefix        data...(40 bit)                        syncPostfix
    HHLL HHLL HHLL HHLL HLLLLLLLLLLLLLLLL (HLLLL HLLLLLLLL HLLLL HLLLLLLLL ....) HLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL

Breakdown:

- four preamble pairs '1'/'0' each with a length of ca. 1000us
- syncPre, syncPost, data0, data1 have a '1' start pulse of ca. 500us
- syncPre pulse before dataPtr has a '0' pulse length of ca. 8000us
- data0 (0-bits) have then a '0' pulse length of ca. 2000us
- data1 (1-bits) have then a '0' pulse length of ca. 4000us
- syncPost after dataPtr has a '0' pulse length of ca. 16000us

This analysis is the reason for the new r_device definitions below.
NB: pulse_slicer_ppm does not use .gap_limit if .tolerance is set.
*/

r_device const infactory = {
        .name        = "inFactory, nor-tec, FreeTec NC-3982-913 temperature humidity sensor",
        .modulation  = OOK_PULSE_PPM,
        .sync_width  = 500,  // Sync pulse width (recognized, but not used)
        .short_width = 2000, // Width of a '0' gap
        .long_width  = 4000, // Width of a '1' gap
        .reset_limit = 5000, // Maximum gap size before End Of Message [us]
        .tolerance   = 750,  // Width interval 0=[1250..2750] 1=[3250..4750], should be quite robust
        .decode_fn   = &infactory_decode,
        .fields      = output_fields,
};
