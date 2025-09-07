/** @file
    RFM69 decoder as used on LowPowerLabs Moteino boards

    Copyright (C) 2025 Ian Cockett <cockettian@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Generic decoder for RFM69 radio modules as used on LowPowerLab.com Moteino boards. Handy if you want to use an SDR to decode various
//  other devices together with some Moteino based nodes all in the same platform.
//  Tested and data captures with sample sketch https://github.com/LowPowerLab/RFM69/blob/master/Examples/Node/Node.ino
//
//  Encryption must be disabled in the sketch (comment out #define ENCRYPTKEY)
//  Tested and data captures from 433MHz RFM69HW_HCW board, but 868MHz models should be similar
//
//  Ian Cockett         Initial Version             Mar 2018
//                      Updated for latest RTL_433  Aug 2025
//
//
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//  Preamble(3) |  Sync (2) |       Header (4)    | Data (n) | CRC (2)
//  0xAAAAAA    | 0x2d,0x64 | Len, Dest, Src, Ctl |          |
//              |           |<--------------65-------------->|
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "decoder.h"
#include "r_api.h"
#include "r_util.h"
#include "data.h"

#define LENGTH_POS      5
#define NODE_ID_POS     7
#define DATA_START_POS  9

#define HEADER_LENGTH   6
#define MAX_LENGTH      65
#define BUF_LENGTH      72      // header + max + 1


static int rfm69_fsk_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t search = 0x2d; // sync
    unsigned posn;

    data_t *data;

    uint8_t message[BUF_LENGTH]; // max size of header + payload + terminator
    uint8_t payload[MAX_LENGTH]; // max size of payload + terminator
    
    posn = bitbuffer_search(bitbuffer, 0, 0, &search, 8);

    if ((posn < 24) || (posn > 28))
        return 0; // Can't find bit position of sync word

    bitbuffer_extract_bytes(bitbuffer, 0, posn - 24, (uint8_t *)&message, (MAX_LENGTH) * 8); // Extract out full into our aligned buffer. Include 3x8 bits of sync word before the preamble

    uint8_t payload_len = message[LENGTH_POS];

    if (payload_len > MAX_LENGTH)
        return 0; // message junk

    bitbuffer_extract_bytes(bitbuffer, 0, posn + 16, (uint8_t *)&payload, (payload_len + 1) * 8); // we need to include length byte in CRC calc

    /*      uncomment for extra message logging

    fprintf(stderr, "\n");
    for (unsigned j = 0; j < maxlen; j++) {
        fprintf(stderr, "%02x ", message[j]);
    }
    fprintf(stderr, "\n");
    for (unsigned j = 0; j < maxlen; j++) {
        fprintf(stderr, "%c ", message[j]);
    }
    fprintf(stderr, "\n");

    */

    uint16_t crc;
    
    // found the polynomial values in an old Semtech appication note.
    crc = ~crc16(payload, (payload_len + 1), 0x1021, 0x1d0f) & 0xffff;

    if (((crc >> 8) != message[HEADER_LENGTH + payload_len + 0]) ||
            ((crc & 0x00ff) != message[HEADER_LENGTH  + payload_len+ 1])) {
        fprintf(stderr, "CRC NOT OK!\n");
        return 0;
    }

    if (message[NODE_ID_POS] == 0x02) 
    {
        message[HEADER_LENGTH + payload_len] = 0x00;                 // remove the checksum which is still at the end of the message buffer

        char strNode[2];    
        char strGateway[2];
        char strMessage[32];

        sprintf(strGateway, "%d", (message[HEADER_LENGTH]));
        sprintf(strNode, "%d", (message[NODE_ID_POS]));
        sprintf(strMessage, "%.30s", &message[DATA_START_POS]);

        /* clang-format off */          
        data = data_make("model",	    "Model",           DATA_STRING, "LowPowerLab.com Moteino RFM69 Node",
                        "gatewayId", 	"GatewayId", 	   DATA_STRING, strGateway,
                        "id",	        "Node Id  ",       DATA_STRING, strNode,
                        "message",      "Msg",             DATA_STRING, strMessage,
                        "mic",           "Integrity",      DATA_STRING, "CHECKSUM",
                        NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);

        return 1;
 
    }

    if (message[NODE_ID_POS] == 0xff)   // your node id
    {
        // Add your own stuff
    }

    return 0;
}

static char const *const output_fields[] = {
        NULL,
};

r_device rfm69_lowpowerlab_moteino = {
        .name        = "RFM69 LowPowerLab Moteino board (-s 1000k)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 18,
        .long_width  = 18,
        .reset_limit = 18432,
        .decode_fn   = &rfm69_fsk_callback,
        .disabled    = 0,
        .fields      = output_fields,

};
