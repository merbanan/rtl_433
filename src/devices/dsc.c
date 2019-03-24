/* DSC sensor
 *
 *
 * Decode DSC security contact messages
 *
 * Copyright (C) 2015 Tommy Vestermark
 * Copyright (C) 2015 Robert C. Terzi
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * DSC - Digital Security Controls 433 Mhz Wireless Security Contacts
 *       doors, windows, smoke, CO2, water,
 *
 * Protocol Description available in this FCC Report for FCC ID F5300NB912
 *  https://apps.fcc.gov/eas/GetApplicationAttachment.html?id=100988
 *
 * General Packet Description
 * - Packets are 26.5 mS long
 * - Packets start with 2.5 mS of constant modulation for most sensors
 *   Smoke/CO2/Fire sensors start with 5.6 mS of constant modulation
 * - The length of a bit is 500 uS, broken into two 250 uS segments.
 *    A logic 0 is 500 uS (2 x 250 uS) of no signal.
 *    A logic 1 is 250 uS of no signal followed by 250 uS of signal/keying
 * - Then there are 4 sync logic 1 bits.
 * - There is a sync/start 1 bit in between every 8 bits.
 * - A zero byte would be 8 x 500 uS of no signal (plus the 250 uS of
 *   silence for the first half of the next 1 bit) for a maximum total
 *   of 4,250 uS (4.25 mS) of silence.
 * - The last byte is a CRC with nothing after it, no stop/sync bit, so
 *   if there was a CRC byte of 0, the packet would wind up being short
 *   by 4 mS and up to 8 bits (48 bits total).
 *
 * There are 48 bits in the packet including the leading 4 sync 1 bits.
 * This makes the packet 48 x 500 uS bits long plus the 2.5 mS preamble
 * for a total packet length of 26.5 ms.  (smoke will be 3.1 ms longer)
 *
 * Packet Decoding
 *    Check intermessage start / sync bits, every 8 bits
 *    Byte 0   Byte 1   Byte 2   Byte 3   Byte 4   Byte 5
 *    vvvv         v         v         v         v
 *    SSSSdddd ddddSddd dddddSdd ddddddSd dddddddS cccccccc  Sync,data,crc
 *    01234567 89012345 67890123 45678901 23456789 01234567  Received Bit No.
 *    84218421 84218421 84218421 84218421 84218421 84218421  Received Bit Pos.
 *
 #    SSSS         S         S         S         S           Synb bit positions
 *        ssss ssss ttt teeee ee eeeeee e eeeeeee  cccccccc  type
 *        tttt tttt yyy y1111 22 223333 4 4445555  rrrrrrrr
 *
 *  Bits: 0,1,2,3,12,21,30,39 should == 1
 *
 *  Status (st) = 8 bits, open, closed, tamper, repeat
 *  Type (ty)   = 4 bits, Sensor type, really first nybble of ESN
 *  ESN (e1-5)  = 20 bits, Electronic Serial Number: Sensor ID.
 *  CRC (cr)    = 8 bits, CRC, type/polynom to be determined
 *
 * The ESN in practice is 24 bits, The type + remaining 5 nybbles,
 *
 * The CRC is 8 bit, reflected (lsb first), Polynomial 0xf5, Initial value 0x3d
 *
 * CRC algorithm found with CRC reveng (reveng.sourceforge.net)
 *
 * CRC Model Parameters:
 * width=8  poly=0xf5  init=0x3d  refin=true  refout=true  xorout=0x00  check=0xfd  name=(none)
 *
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
    uint32_t esn;
    char status_str[3];
    char esn_str[7];
    int s_closed, s_event, s_tamper, s_battery_low;
    int s_xactivity, s_xtamper1, s_xtamper2, s_exception;

    if (decoder->verbose > 1) {
        fprintf(stderr,"Possible DSC Contact: ");
        bitbuffer_print(bitbuffer);
    }

    for (int row = 0; row < bitbuffer->num_rows; row++) {
        if (decoder->verbose > 1 && bitbuffer->bits_per_row[row] > 0 ) {
            fprintf(stderr,"row %d bit count %d\n", row,
                bitbuffer->bits_per_row[row]);
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
            if (decoder->verbose > 1 && bitbuffer->bits_per_row[row] > 0) {
            fprintf(stderr,"DSC row %d invalid bit count %d\n",
                row, bitbuffer->bits_per_row[row]);
            }
            continue;
        }

        b = bitbuffer->bb[row];
        // Validate Sync/Start bits == 1 and are in the right position
        if (!((b[0] & 0xF0) &&     // First 4 bits are start/sync bits
              (b[1] & 0x08) &&    // Another sync/start bit between
              (b[2] & 0x04) &&    // every 8 data bits
              (b[3] & 0x02) &&
              (b[4] & 0x01))) {
            if (decoder->verbose > 1) {
                fprintf(stderr, "DSC Invalid start/sync bits ");
                bitrow_print(b, 40);
            }
            continue;
        }

        bytes[0] = ((b[0] & 0x0F) << 4) | ((b[1] & 0xF0) >> 4);
        bytes[1] = ((b[1] & 0x07) << 5) | ((b[2] & 0xF8) >> 3);
        bytes[2] = ((b[2] & 0x03) << 6) | ((b[3] & 0xFC) >> 2);
        bytes[3] = ((b[3] & 0x01) << 7) | ((b[4] & 0xFE) >> 1);
        bytes[4] = ((b[5]));

        // XXX change to decoder->verbose
        if (decoder->verbose) {
            fprintf(stdout, "DSC Contact Raw Data: ");
            bitrow_print(bytes, 40);
        }

        status = bytes[0];
        esn = (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
        crc = bytes[4];

        if (crc8le(bytes, DSC_CT_MSGLEN, 0xf5, 0x3d) != 0) {
            if (decoder->verbose)
                fprintf(stderr,"DSC Contact bad CRC: %06X, Status: %02X, CRC: %02X\n",
                        esn, status, crc);
            continue;
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
        // 0x01 Cleared tamper status (seend during hearbeats)
        s_tamper = ((status & 0x01) != 0x01) || ((status & 0x10) == 0x10);

        // "experimental" (naming might change)
        s_xactivity = (status & 0x20) == 0x20;

        // Break out 2 tamper bits
        s_xtamper1 = (status & 0x01) != 0x01; // 0x01 set: case closed/no tamper
        s_xtamper2 = (status & 0x10) == 0x10; //tamper event or EOL problem

        // exception/states not seen
        // 0x80 is always set and 0x04 has never been set.
        s_exception = ((status & 0x80) != 0x80) || ((status & 0x04) == 0x04);

        sprintf(status_str, "%02x", status);
        sprintf(esn_str, "%06x", esn);


        data = data_make(
                "model", "", DATA_STRING, _X("DSC-Security","DSC Contact"),
                "id", "", DATA_INT, esn,
                "closed", "", DATA_INT, s_closed, // @todo make bool
                "event", "", DATA_INT, s_event, // @todo make bool
                "tamper", "", DATA_INT, s_tamper, // @todo make bool
                _X("battery_ok","battery_low"), "", DATA_INT, _X(!s_battery_low,s_battery_low), // @todo make bool
                "xactivity", "", DATA_INT, s_xactivity, // @todo make bool

                // Note: the following may change or be removed
                "xtamper1", "", DATA_INT, s_xtamper1, // @todo make bool
                "xtamper2", "", DATA_INT, s_xtamper2, // @todo make bool
                "exception", "", DATA_INT, s_exception, // @todo make bool
                "esn", "", DATA_STRING, esn_str, // to be removed - transitional
                "status", "", DATA_INT, status,
                "status_hex", "", DATA_STRING, status_str, // to be removed - once bits are output
                "mic", "", DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);

        valid_cnt++; // Have a valid packet.
    }

    if (valid_cnt) {
        return 1;
    }

    return 0;
}

static char *output_fields[] = {
    "model",
    "id",
    "status",
    "battery_ok",
    "mic",
    NULL
};

r_device DSC = {
    .name          = "DSC Security Contact",
    .modulation    = OOK_PULSE_PCM_RZ,
    .short_width   = 250,    // Pulse length, 250 µs
    .long_width    = 500,    // Bit period, 500 µs
    .reset_limit   = 5000, // Max gap,
    .decode_fn     = &dsc_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
