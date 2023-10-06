/** @file
    DSC security contact sensors.

    Copyright (C) 2015 Tommy Vestermark
    Copyright (C) 2015 Robert C. Terzi

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
DSC - Digital Security Controls 433 Mhz Wireless Security Contacts
doors, windows, smoke, CO2, water.

Protocol Description available in this FCC Report for FCC ID F5300NB912
https://apps.fcc.gov/eas/GetApplicationAttachment.html?id=100988

General Packet Description
- Packets are 26.5 mS long
- Packets start with 2.5 mS of constant modulation for most sensors
  Smoke/CO2/Fire sensors start with 5.6 mS of constant modulation
- The length of a bit is 500 uS, broken into two 250 uS segments.
   A logic 0 is 500 uS (2 x 250 uS) of no signal.
   A logic 1 is 250 uS of no signal followed by 250 uS of signal/keying
- Then there are 4 sync logic 1 bits.
- There is a sync/start 1 bit in between every 8 bits.
- A zero byte would be 8 x 500 uS of no signal (plus the 250 uS of
  silence for the first half of the next 1 bit) for a maximum total
  of 4,250 uS (4.25 mS) of silence.
- The last byte is a CRC with nothing after it, no stop/sync bit, so
  if there was a CRC byte of 0, the packet would wind up being short
  by 4 mS and up to 8 bits (48 bits total).
- Note the WS4945 doubles the length of those timings.

There are 48 bits in the packet including the leading 4 sync 1 bits.
This makes the packet 48 x 500 uS bits long plus the 2.5 mS preamble
for a total packet length of 26.5 ms.  (smoke will be 3.1 ms longer)

Packet Decoding

    Check intermessage start / sync bits, every 8 bits
    Byte 0   Byte 1   Byte 2   Byte 3   Byte 4   Byte 5
    vvvv         v         v         v         v
    SSSSdddd ddddSddd dddddSdd ddddddSd dddddddS cccccccc  Sync,data,crc
    01234567 89012345 67890123 45678901 23456789 01234567  Received Bit No.
    84218421 84218421 84218421 84218421 84218421 84218421  Received Bit Pos.

    SSSS         S         S         S         S           Synb bit positions
        ssss ssss ttt teeee ee eeeeee e eeeeeee  cccccccc  type
        tttt tttt yyy y1111 22 223333 4 4445555  rrrrrrrr

- Bits: 0,1,2,3,12,21,30,39 should == 1

- Status (st) = 8 bits, open, closed, tamper, repeat
- Type (ty)   = 4 bits, Sensor type, really first nybble of ESN
- ESN (e1-5)  = 20 bits, Electronic Serial Number: Sensor ID.
- CRC (cr)    = 8 bits, CRC, type/polynom to be determined

The ESN in practice is 24 bits, The type + remaining 5 nybbles.
The physical devices have all 6 digits printed in hex. Devices are enrolled
by entering or recording the 6 hex digits.

The CRC is 8 bit, reflected (lsb first), Polynomial 0xf5, Initial value 0x3d

Status bit breakout:

The status byte contains a number of bits that indicate:
-  open vs closed
- event vs heartbeat
- battery ok vs low
- tamper
- recent activity (for certain devices)

The majority of the DSC sensors use the status bits the same way.
There are some slight differences depending on who made the device.

@todo - the status bits don't make sense for the one-way keyfob
and should be broken out two indicate which buttons are pressed.
The keyfob can be detected by the type nybble.

Notes:
- The device type nybble isn't really useful other than for detecting
  the keyfob. For example door/window contacts (Type 2) are used pretty
  generically, so the same type can be used for burglar, flood, fire,
  temperature limits, etc.  The device type is mildly informational
  during testing and discovery. It can easily be seen as the  first digit
  of the ESN, so it doesn't need to be broken out separately.
- There seem to be two bits used inconsistently to indicate whether
  the sensor is being tampered with (case opened, removed from the wall,
  missing EOL resistor, etc.
- The two-way devices wireless keypad and use an entirely different
  modulation. They are supposed to be encrypted. A sampling rate
  greater than 250 khz (1 mhz?) looks to be necessary.
- Tested on EV-DW4927 door/glass break sensor, WS4975 door sensor,
  WS4945 door sensor and WS4904P motion sensors.
- The EV-DW4927 combined door / glass break sensor sends out two
  separate signals. Glass break uses the original ESN as written on
  the case and door sensor uses ESN with last digit +1.

*/

#include "decoder.h"

#define DSC_CT_MSGLEN        5

static int dsc_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;
    int valid_cnt = 0;
    uint8_t bytes[5];
    uint8_t status, crc;
    //int subtype;
    uint32_t esn;
    int s_closed, s_event, s_tamper, s_battery_low;
    int s_xactivity, s_xtamper1, s_xtamper2, s_exception;

    int result = 0;

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] > 0) {
            decoder_logf(decoder, 2, __func__, "row %d bit count %d",
                    row, bitbuffer->bits_per_row[row]);
        }

        // Number of bits in the packet should be 48 but due to the
        // encoding of trailing zeros is a guess based on reset_limit /
        // long_width (bit period).  With current values up to 10 zero
        // bits could be added, so it is normal to get a 58 bit packet.
        //
        // If the limits are changed for some reason, the max number of bits
        // will need to be changed as there may be more zero bit padding
        if (bitbuffer->bits_per_row[row] < 48 ||
            bitbuffer->bits_per_row[row] > 70) {  // should be 48 at most
            if (bitbuffer->bits_per_row[row] > 0) {
                decoder_logf(decoder, 2, __func__, "row %d invalid bit count %d",
                        row, bitbuffer->bits_per_row[row]);
            }
            result = DECODE_ABORT_EARLY;
            continue; // DECODE_ABORT_EARLY
        }

        b = bitbuffer->bb[row];
        // Validate Sync/Start bits == 1 and are in the right position
        if (!((b[0] & 0xF0) &&     // First 4 bits are start/sync bits
              (b[1] & 0x08) &&    // Another sync/start bit between
              (b[2] & 0x04) &&    // every 8 data bits
              (b[3] & 0x02) &&
              (b[4] & 0x01))) {
            decoder_log_bitrow(decoder, 2, __func__, b, 40, "Invalid start/sync bits ");
            result = DECODE_ABORT_EARLY;
            continue; // DECODE_ABORT_EARLY
        }

        bytes[0] = ((b[0] & 0x0F) << 4) | ((b[1] & 0xF0) >> 4);
        bytes[1] = ((b[1] & 0x07) << 5) | ((b[2] & 0xF8) >> 3);
        bytes[2] = ((b[2] & 0x03) << 6) | ((b[3] & 0xFC) >> 2);
        bytes[3] = ((b[3] & 0x01) << 7) | ((b[4] & 0xFE) >> 1);
        bytes[4] = ((b[5]));

        // prevent false positive of: ff ff ff ff 00
        if (bytes[0] == 0xff && bytes[1] == 0xff && bytes[2] == 0xff && bytes[3] == 0xff) {
            result = DECODE_FAIL_SANITY;
            continue; // DECODE_FAIL_SANITY
        }

        decoder_log_bitrow(decoder, 1, __func__, bytes, 40, "Contact Raw Data");

        status = bytes[0];
        //subtype = bytes[1] >> 4;  // @todo needed for detecting keyfob
        esn = (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
        crc = bytes[4];

        if (crc8le(bytes, DSC_CT_MSGLEN, 0xf5, 0x3d) != 0) {
            decoder_logf(decoder, 1, __func__, "Contact bad CRC: %06X, Status: %02X, CRC: %02X",
                        esn, status, crc);
            result = DECODE_FAIL_MIC;
            continue; // DECODE_FAIL_MIC
        }

        // Decode status bits:

        // 0x02 = Closed/OK/Restored
        s_closed = (status & 0x02) == 0x02;

        // 0x40 = Heartbeat (not an open/close event)
        s_event = ((status & 0x40) != 0x40);

        // 0x08 Battery Low
        s_battery_low = (status & 0x08) == 0x08;

        // Tamper: 0x10 set or 0x01 unset indicate tamper
        // 0x10 Set to tamper message type (more testing needed)
        // 0x01 Cleared tamper status (seen during heartbeats)
        s_tamper = ((status & 0x01) != 0x01) || ((status & 0x10) == 0x10);

        // "experimental" (naming might change)
        s_xactivity = (status & 0x20) == 0x20;

        // Break out 2 tamper bits
        s_xtamper1 = (status & 0x01) != 0x01; // 0x01 set: case closed/no tamper
        s_xtamper2 = (status & 0x10) == 0x10; //tamper event or EOL problem

        // exception/states not seen
        // 0x80 is always set and 0x04 has never been set.
        s_exception = ((status & 0x80) != 0x80) || ((status & 0x04) == 0x04);

        char status_str[3];
        snprintf(status_str, sizeof(status_str), "%02x", status);
        char esn_str[7];
        snprintf(esn_str, sizeof(esn_str), "%06x", esn);

        /* clang-format off */
        data = data_make(
                "model",        "",             DATA_STRING, "DSC-Security",
                "id",           "",             DATA_INT,    esn,
                "closed",       "",             DATA_INT,    s_closed, // @todo make bool
                "event",        "",             DATA_INT,    s_event, // @todo make bool
                "tamper",       "",             DATA_INT,    s_tamper, // @todo make bool
                "battery_ok",   "Battery",      DATA_INT,    !s_battery_low,
                "xactivity",    "",             DATA_INT,    s_xactivity, // @todo make bool

                // Note: the following may change or be removed
                "xtamper1",     "",             DATA_INT,    s_xtamper1, // @todo make bool
                "xtamper2",     "",             DATA_INT,    s_xtamper2, // @todo make bool
                "exception",    "",             DATA_INT,    s_exception, // @todo make bool
                "esn",          "",             DATA_STRING, esn_str, // to be removed - transitional
                "status",       "",             DATA_INT,    status,
                "status_hex",   "",             DATA_STRING, status_str, // to be removed - once bits are output
                "mic",          "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);

        valid_cnt++; // Have a valid packet.
    }

    if (valid_cnt) {
        return 1;
    }

    // Only returns the latest result, but better than nothing.
    return result;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "closed",
        "event",
        "tamper",
        "status",
        "battery_ok",
        "esn",
        "exception",
        "status_hex",
        "xactivity",
        "xtamper1",
        "xtamper2",
        "mic",
        NULL,
};

r_device const dsc_security = {
        .name        = "DSC Security Contact",
        .modulation  = OOK_PULSE_RZ,
        .short_width = 250,  // Pulse length, 250 µs
        .long_width  = 500,  // Bit period, 500 µs
        .reset_limit = 5000, // Max gap,
        .decode_fn   = &dsc_callback,
        .fields      = output_fields,
};

r_device const dsc_security_ws4945 = {
        // Used for EV-DW4927, WS4975 and WS4945.
        .name        = "DSC Security Contact (WS4945)",
        .modulation  = OOK_PULSE_RZ,
        .short_width = 536,  // Pulse length, 536 µs
        .long_width  = 1072, // Bit period, 1072 µs
        .reset_limit = 9000, // Max gap, based on 8 zero bits between sync bit
        .decode_fn   = &dsc_callback,
        .fields      = output_fields,
};
