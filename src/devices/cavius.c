/*** @file
    Cavius protocol.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/** Cavius smoke, heat and water detectors

    The alarm units use HopeRF RF69 chips on 869.67 MHz. FSK modulation, 4800 bps
    They seem to use 'Cavi' as a sync word on the chips
    Everything after the sync word is Manchester coded.
    The unpacked payload is 11 bytes long structured as follows:

    NNNNMMCSSSS

    N = Network ID (Device ID of the Master device)
    M = Message bytes. Second byte is the first byte inverted (0xF ^ M)
    C = CRC-8 (Maxim type) of NNNNMM (the first 6 bytes in the payload)
    S = Sending device ID

    Message bits as far as we can tell:

    0x80 = PAIRING
    0x40 = TEST
    0x20 = ALARM
    0x10 = WARNING
    0x08 = BATTLOW
    0x04 = MUTE
    0x02 = UNKNOWN2
    0x01 = UNKNOWN1

    Sometimes the receiver samplerate has to be at 250ksps to decode properly.
*/

#include "decoder.h"

enum cavius_message {
    cavius_pairing  = 0x80,
    cavius_test     = 0x40,
    cavius_alarm    = 0x20,
    cavius_warning  = 0x10,
    cavius_battlow  = 0x08,
    cavius_mute     = 0x04,
    cavius_unknown2 = 0x02,
    cavius_unknown1 = 0x01,
};

/** @fn int cavius_callback(r_device *decoder, bitbuffer_t *bitbuffer)
    Cavius smoke alarm decoder.
*/
static int cavius_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;

    static const uint8_t SYNC[]  = {0x43, 0x61, 0x76, 0x69};

    // Find the sync
    unsigned bit_offset = bitbuffer_search(bitbuffer, 0, 0, SYNC, sizeof(SYNC)*8);
    if (bit_offset + 22*8 >= bitbuffer->bits_per_row[0]) {  // Did not find a big enough package
        return 0;
    }
    bit_offset += sizeof(SYNC)*8;     // skip sync

    bitbuffer_t databits = {0};

    bitbuffer_manchester_decode(bitbuffer, 0, bit_offset, &databits, 11*8);
    bitbuffer_invert(&databits);

    if(decoder->verbose) {
        bitbuffer_print(&databits);
    }

    uint32_t net_id    = 0;
    uint8_t  message   = 0;
    uint8_t  message_i = 0;
    uint8_t  crc       = 0;
    uint32_t sender_id = 0;
    uint8_t  crc_calced = crc8le(databits.bb[0], 6, 0x131, 0x0);

    uint8_t  crcok = 0;

    bitbuffer_extract_bytes(&databits, 0, 0*8, &net_id,    4*8);
    bitbuffer_extract_bytes(&databits, 0, 4*8, &message,   1*8);
    bitbuffer_extract_bytes(&databits, 0, 5*8, &message_i, 1*8);
    bitbuffer_extract_bytes(&databits, 0, 6*8, &crc,       1*8);
    bitbuffer_extract_bytes(&databits, 0, 7*8, &sender_id, 4*8);

    if(crc == crc_calced) crcok = 1;

    // Some uglyish bit banging here. Extraction seems to change endianness?
    uint32_t net_id_big = ((net_id>>24)&0xff) | // move byte 3 to byte 0
                          ((net_id<<8)&0xff0000) | // move byte 1 to byte 2
                          ((net_id>>8)&0xff00) | // move byte 2 to byte 1
                          ((net_id<<24)&0xff000000); // byte 0 to byte 3

    uint32_t sender_id_big = ((sender_id>>24)&0xff) | // move byte 3 to byte 0
                             ((sender_id<<8)&0xff0000) | // move byte 1 to byte 2
                             ((sender_id>>8)&0xff00) | // move byte 2 to byte 1
                             ((sender_id<<24)&0xff000000); // byte 0 to byte 3

    char *text = "Unknown";

    switch (message) {
        case cavius_alarm:
            text = "Fire alarm";
            break;
        case cavius_battlow:
            text = "Battery low";
            break;
        case cavius_mute:
            text = "Alarm muted";
            break;
        case cavius_pairing:
            text = "Pairing";
            break;
        case cavius_test:
            text = "Test alarm";
            break;
        case cavius_warning:
            text = "Warning/Water detected";
            break;
        default:
            break;
    }

    //printf("Net: %d Sender: %d Message %02x\n", net_id_big, sender_id, message);
    //printf("CRC_calced: %d OK: %d\n", crc_calced, crcok);

    /* clang-format off */
    data = data_make(
            "netid",         "Net ID",      DATA_INT,    net_id_big,
            "senderid",      "Sender ID",   DATA_INT,    sender_id_big,
            "message",       "Message",     DATA_INT,    message,
            "text",          "Description", DATA_STRING, text,
            "crc",           "CRC-8",       DATA_INT,    crc,
            "crcok",         "CRC OK",      DATA_INT,    crcok,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "netid",
        "senderid",
        "message",
        "text",
        "crc",
        "crcok",
        NULL,
};

r_device cavius = {
        .name        = "Cavius",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 206,
        .long_width  = 206,
        .sync_width  = 2700,
        .gap_limit   = 1000,
        .reset_limit = 1000,
        .decode_fn   = &cavius_callback,
        .disabled    = 0,
        .fields      = output_fields,
};
