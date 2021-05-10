/** @file
    Rojaflex shutter and remote devices.

    Copyright (c) 2021 Sebastian Hofmann <sebastian.hofmann+rtl433@posteo.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include "decoder.h"

/**
Frequency: 433.92MH
.modulation  = FSK_PULSE_PCM,
.short_width = 100,
.long_width  = 100,
.reset_limit = 102400,
*/

/**
Signal documentation

Default signal layout
0xaaaaaaaa d391d391 SS KKKKKK ?CDDDD TTTT CCCC

4 Bytes Preamble
4 Bytes Sync Word
1 Byte  Size       "S" is always "0x08"
3 Bytes ID         "Seems to be the static ID for the Homeinstallation"
3 Bytes Data       "See docu below"
2 Bytes Token      "It seems to be a message token which is used for the shutter answer."
2 Bytes CRC-16/CMS "Seems optional, because this is missing from commands via bridge P2D."
19 Byte Summe      "Only 17 Bytes without CRC (from bridge)"

Data documentation
0xFF     - Size always "0x8"
0xFFFFFF - ID, I assume that differs per installation, but is static then
0xF      - Unknown (is static 0x2) - Not sure if it is also the HomeID
0xF      - Channel: 1-15 single channels (one shutter is registert to one channel), 0 means all
0xFF     - Command ID    (0x0a = stop, 0x1a = up,0x8a = down, 0xea = Request)
0xFF     - Command Value (in status from shutter this is the percent value. 0% for open 100% for close)

Message Token documentation
See generator function, no clue how to calculate it dynamic

To get raw data:
./rtl_433 -f 433920000 -X n=rojaflex,m=FSK_PCM,s=100,l=100,r=102400
*/

// Message Defines
#define DATAFRAME_BITCOUNT_INCL_CRC 88
#define DATAFRAME_BYTECOUNT_INCL_CRC 11 //Including CRC but no pramble
#define LENGTH_OFFSET 0
#define LENGTH_BITCOUNT 8
#define ID_OFFSET 1 // HomeID which I assume is static for one Remote Device
#define ID_BITCOUNT 28
#define CHANNEL_OFFSET 4         // Mask 0x0F
#define UNKNOWN_CHANNEL_OFFSET 5 // Mask 0xF0
#define COMMAND_ID_OFFSET 5
#define COMMAND_ID_BITCOUNT 8
#define COMMAND_VALUE_OFFSET 6
#define COMMAND_VALUE_BITCOUNT 8
#define MESSAGE_TOKEN_OFFSET 7
#define MESSAGE_TOKEN_BITCOUNT 16
#define MESSAGE_CRC_OFFSET 9
#define MESSAGE_CRC_BITCOUNT 16

//Command Defindes
#define COMMAND_ID_STOP 0x0a
#define COMMAND_ID_UP 0x1a
#define COMMAND_ID_DOWN 0x8a
#define COMMAND_ID_SAVE_UNSAVE_POS 0x9a
#define COMMAND_ID_GO_SAVED_POS 0xda
#define COMMAND_ID_REQUESTSTATUS 0xea

// Calcualte message token in static way
// It seems to be a more dynamic formular ... but I did not find it until now.
static uint16_t calcMessageToken(uint8_t command, uint8_t channel)
{
    uint16_t token             = 0;
    uint8_t magic_static_value = 0;

    // Calculate some magic static value because I do not know the dynamic way.
    switch (command) {
    case COMMAND_ID_STOP:
        magic_static_value = 0x85;
        break;
    case COMMAND_ID_UP:
        magic_static_value = 0xa5;
        break;
    case COMMAND_ID_DOWN:
        magic_static_value = 0x85;
        break;
    case COMMAND_ID_SAVE_UNSAVE_POS:
        magic_static_value = 0xa5;
        break;
    case COMMAND_ID_GO_SAVED_POS:
        magic_static_value = 0x25;
        break;
    case COMMAND_ID_REQUESTSTATUS:
        magic_static_value = 0x5D;
        break;
    default:
        return 0xFFFF;
    }

    switch (command) {
    case COMMAND_ID_STOP:
    case COMMAND_ID_UP:
    case COMMAND_ID_DOWN:
    case COMMAND_ID_SAVE_UNSAVE_POS:
    case COMMAND_ID_GO_SAVED_POS:
        token = command << 8;
        break;
    case COMMAND_ID_REQUESTSTATUS:
        // I do not know why not also the command
        token = 0x02 << 8;
        break;
    }

    token = token + magic_static_value + channel;

    return token;
}

// Done
static char *getCommandString(uint8_t *msg)
{
    switch (msg[COMMAND_ID_OFFSET]) {
    case COMMAND_ID_STOP:
        return "0x0a - Stop     ";
    case COMMAND_ID_UP:
        return "0x1a - Up       ";
    case COMMAND_ID_DOWN:
        return "0x8a - Down     ";
    case COMMAND_ID_SAVE_UNSAVE_POS:
        // 5 x Stop on remote set inclined pos.
        // Command is complete identical for set and unset
        // - If nothing is saved it will set.
        // - If something is saved and the position is identical it will reset.
        //   The P2D bridge is beeping in that case.
        return "0x9a - Save/Unsave position";
    case COMMAND_ID_GO_SAVED_POS:
        // Hold Stop for 5 seconds to drive to saved pos.
        return "0xda - Go saved position";
    case COMMAND_ID_REQUESTSTATUS:
        // I am not sure if that is true.
        // I know that the remote is sending the message and not the shutter.
        // I know that the bridge is not sending this message after e.g.0x1a.
        // I know that the shutter sends a Position status right after this message.
        // After the normal 0x1a command from a bridge, the position status
        // will be send wenn the stutter is completely up but not before.
        // So I think this is a "Request Shutter Status Now".
        return "0xea - Request Status    ";
    case 0x85: //  0%
        return "0x85 - Pos. Status -   0%";
    case 0x95: // 20%
        return "0x95 - Pos. Status -  20%";
    case 0xA5: // 40%
        return "0xA5 - Pos. Status -  40%";
    case 0xB5: // 60%
        return "0xB5 - Pos. Status -  60%";
    case 0xC5: // 80%
        return "0xC5 - Pos. Status -  80%";
    case 0xD5: //100%
        return "0xD5 - Pos. Status - 100%";
    }
    return "unknown";
}

static int rojaflex_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const message_preamble[] = {
            /*0xaa, 0xaa,*/ 0xaa, 0xaa, // preamble
            0xd3, 0x91, 0xd3, 0x91      // sync word
    };

    data_t *data;
    uint8_t msg[DATAFRAME_BYTECOUNT_INCL_CRC];
    uint8_t dataframe_bitcount = 0;

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_EARLY;
    }

    int row = 0;
    // Validate message and reject it as fast as possible : check for preamble
    unsigned start_pos = bitbuffer_search(bitbuffer, row, 0, message_preamble, sizeof(message_preamble) * 8);

    if (start_pos < bitbuffer->bits_per_row[row]) {
        // Save bitcount of total message including preamble
        dataframe_bitcount = (bitbuffer->bits_per_row[row] - start_pos - sizeof(message_preamble) * 8) & 0xFE;
    }
    else {
        return DECODE_ABORT_EARLY; // no preamble detected
    }

    if (dataframe_bitcount < (DATAFRAME_BITCOUNT_INCL_CRC - MESSAGE_CRC_BITCOUNT) || (dataframe_bitcount > DATAFRAME_BITCOUNT_INCL_CRC)) {
        // check min and max length
        return DECODE_ABORT_LENGTH;
    }

    //Extract raw line
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + sizeof(message_preamble) * 8, msg, dataframe_bitcount);
    if (decoder->verbose > 1) {
        bitrow_printf(msg, dataframe_bitcount, "%s: frame data: ", __func__);
    }

    // Check CRC if available
    if (dataframe_bitcount == DATAFRAME_BITCOUNT_INCL_CRC) {
        // Thanks to: ./reveng -w 16 -s $msg1 $msg2 $msg3
        // width=16  poly=0x8005  init=0xffff  refin=false  refout=false  xorout=0x0000  check=0xaee7  residue=0x0000  name="CRC-16/CMS"
        uint16_t crc_message = (msg[MESSAGE_CRC_OFFSET] << 8 | msg[MESSAGE_CRC_OFFSET + 1]);
        uint16_t crc_calc    = crc16(&msg[LENGTH_OFFSET], 9, 0x8005, 0xffff);

        if (crc_message != crc_calc) {
            if (decoder->verbose)
                fprintf(stderr, "%s: CRC invalid message:%04x != calc:%04x\n", __func__, crc_message, crc_calc);

            return DECODE_FAIL_MIC;
        };
    }

    // Build the default terminal output
    {
        char tokenString[7] = "";
        char id[10]         = "";
        char *deviceType    = "unknown";
        sprintf(tokenString, "0x%02x%02x", msg[MESSAGE_TOKEN_OFFSET], msg[MESSAGE_TOKEN_OFFSET + 1]);
        sprintf(id, "0x%02x%02x%02x%01x", msg[ID_OFFSET], msg[ID_OFFSET + 1], msg[ID_OFFSET + 2], (msg[ID_OFFSET + 3] >> 4));

        if ((msg[COMMAND_ID_OFFSET] & 0xF) == 0x5) {
            deviceType = "RojaFlex-Shutter";
        }
        else if ((msg[COMMAND_ID_OFFSET] & 0xF) == 0xa) {
            // Rojaflex Bridge clones a remote signal but does not send an CRC!?!?
            // So we can detect if it a real remote or a bridge on the message length.
            if (dataframe_bitcount == DATAFRAME_BITCOUNT_INCL_CRC) {
                deviceType = "RojaFlex-Remote";
            }
            else {
                deviceType = "RojaFlex-Bridge";
            }
        }

        /* clang-format off */
        data = data_make(
                    "model",        "Model",     DATA_STRING, deviceType,
                    "id",           "ID",        DATA_STRING, id,
                    "channel",      "Channel",   DATA_INT,    msg[CHANNEL_OFFSET] & 0xF,
                    "token",        "Msg Token", DATA_STRING, tokenString,
                    "commandtype",  "Command ",  DATA_STRING, getCommandString(&msg[0]),
                    "commandvalue", "Value",     DATA_INT,    msg[COMMAND_VALUE_OFFSET],
                    "mic",          "Integrity", DATA_STRING, (dataframe_bitcount == DATAFRAME_BITCOUNT_INCL_CRC) ? "CRC" : "",
                    NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
    }

// You can use this defines to clone / generate all commands for other bridges
#define GENERATE_COMMANDS_FOR_CURRENT_CHANNEL 0
#define GENERATE_COMMANDS_FOR_ALL_CHANNELS 0

    if (GENERATE_COMMANDS_FOR_CURRENT_CHANNEL || GENERATE_COMMANDS_FOR_ALL_CHANNELS) {
        uint8_t const remote_commands[] = {
                COMMAND_ID_STOP,
                COMMAND_ID_UP,
                COMMAND_ID_DOWN,
                COMMAND_ID_SAVE_UNSAVE_POS,
                COMMAND_ID_GO_SAVED_POS,
                COMMAND_ID_REQUESTSTATUS};

        uint8_t channel = GENERATE_COMMANDS_FOR_CURRENT_CHANNEL ? msg[CHANNEL_OFFSET] & 0xF : 0;
        uint8_t command = 0;
        uint8_t msg_new[19];

        fprintf(stderr, "\n");
        fprintf(stderr, "%s: Signal cloner\n", __func__);
        fprintf(stderr, "%s: \n", __func__);

        do {
            for (uint8_t i = 0; i < sizeof(remote_commands); ++i) {
                command = remote_commands[i];

                // Create complete message preamble
                msg_new[0] = 0xaa;
                msg_new[1] = 0xaa;
                msg_new[2] = 0xaa;
                msg_new[3] = 0xaa;
                msg_new[4] = 0xd3;
                msg_new[5] = 0x91;
                msg_new[6] = 0xd3;
                msg_new[7] = 0x91;

                // Set length
                msg_new[8 + LENGTH_OFFSET] = 0x8;

                // Clone ID from received message
                msg_new[8 + ID_OFFSET + 0] = msg[ID_OFFSET + 0];
                msg_new[8 + ID_OFFSET + 1] = msg[ID_OFFSET + 1];
                msg_new[8 + ID_OFFSET + 2] = msg[ID_OFFSET + 2];

                // Clone 4bit ID + Channel
                msg_new[8 + ID_OFFSET + 3] = msg[ID_OFFSET + 3] & 0xF0 + channel;

                // Set command id + command value
                msg_new[8 + COMMAND_ID_OFFSET]    = command;
                msg_new[8 + COMMAND_VALUE_OFFSET] = 0x1;

                // Generate message token
                uint16_t token_calc                   = calcMessageToken(command, channel);
                msg_new[8 + MESSAGE_TOKEN_OFFSET + 0] = token_calc >> 8;
                msg_new[8 + MESSAGE_TOKEN_OFFSET + 1] = token_calc & 0xFF;

                // Generate CRC
                // Thanks to: ./reveng -w 16 -s $msg1 $msg2 $msg3
                // width=16  poly=0x8005  init=0xffff  refin=false  refout=false  xorout=0x0000  check=0xaee7  residue=0x0000  name="CRC-16/CMS"
                uint16_t crc_calc                   = crc16(&msg_new[8 + LENGTH_OFFSET], 9, 0x8005, 0xffff);
                msg_new[8 + MESSAGE_CRC_OFFSET + 0] = crc_calc >> 8;
                msg_new[8 + MESSAGE_CRC_OFFSET + 1] = crc_calc & 0xFF;

                /*
                // Print final command
                */
                bitrow_printf(&msg_new[0], sizeof(msg_new) * 8, "%s: CH:%01x Command:0x%02x: ", __func__, channel, command);
            }

            fprintf(stderr, "\n");
            ++channel;
        } while (channel <= 0xF && GENERATE_COMMANDS_FOR_ALL_CHANNELS);
    }

    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "token",
        "commandtype",
        "commandvalue",
        "mic",
        NULL,
};

r_device rojaflex = {
        .name        = "Rojaflex",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 102400,
        .sync_width  = 0,
        .decode_fn   = &rojaflex_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
