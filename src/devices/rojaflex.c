/** @file
    RojaFlex shutter and remote devices.

    Copyright (c) 2021 Sebastian Hofmann <sebastian.hofmann+rtl433@posteo.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include "decoder.h"

/**
RojaFlex shutter and remote devices.

- Frequency: 433.92 MHz

Data layout:

    0xaaaaaaaa d391d391 SS KKKKKK ?CDDDD TTTT CCCC

- 4 byte Preamble   : "0xaaaaaaaa"
- 4 byte Sync Word  : "9391d391"
- 1 byte Size       : "S" is always "0x08"
- 3 byte ID         : Seems to be the static ID for the Homeinstallation
- 3 byte Data       : See below
- 1 byte Token I    : It seems to be an internal message token which is used for the shutter answer.
- 1 byte Token II   : Is the sum of 3 Bytes ID + 3 Bytes Data + 1 Byte token
- 2 byte CRC-16/CMS : poly 0x8005 init 0xffff, seems optional, missing from commands via bridge P2D.

Overall 19 byte packets, only 17 byte without CRC (from bridge).

Data documentation:

- 0xFF     - Size always "0x8"
- 0xFFFFFF - ID, I assume that differs per installation, but is static then
- 0xF      - Unknown (is static 0x2) - Not sure if it is also the HomeID
- 0xF      - Channel: 1-15 single channels (one shutter is registered to one channel), 0 means all
- 0xFF     - Command ID    (0x0a = stop, 0x1a = up,0x8a = down, 0xea = Request)
- 0xFF     - Command Value (in status from shutter this is the percent value. 0% for open 100% for close)

To get raw data:

    ./rtl_433 -f 433920000 -X n=RojaFlex,m=FSK_PCM,s=100,l=100,r=102400
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

// Command Defindes
#define COMMAND_ID_STOP 0x0a
#define COMMAND_ID_UP 0x1a
#define COMMAND_ID_DOWN 0x8a
#define COMMAND_ID_SAVE_UNSAVE_POS 0x9a
#define COMMAND_ID_GO_SAVED_POS 0xda
#define COMMAND_ID_REQUESTSTATUS 0xea

#define DEVICE_TYPE_UNKNOWN 0x0
#define DEVICE_TYPE_SHUTTER 0x5
#define DEVICE_TYPE_REMOTE 0xa
#define DEVICE_TYPE_BRIDGE 0xb

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

    // Extract raw line
    bitbuffer_extract_bytes(bitbuffer, row, start_pos + sizeof(message_preamble) * 8, msg, dataframe_bitcount);
    decoder_log_bitrow(decoder, 2, __func__, msg, dataframe_bitcount, "frame data");

    // Check CRC if available
    if (dataframe_bitcount == DATAFRAME_BITCOUNT_INCL_CRC) {
        uint16_t crc_message = (msg[MESSAGE_CRC_OFFSET] << 8 | msg[MESSAGE_CRC_OFFSET + 1]);
        uint16_t crc_calc    = crc16(&msg[LENGTH_OFFSET], 9, 0x8005, 0xffff); // "CRC-16/CMS"

        if (crc_message != crc_calc) {
            decoder_logf(decoder, 1, __func__, "CRC invalid message:%04x != calc:%04x", crc_message, crc_calc);

            return DECODE_FAIL_MIC;
        }
    }

    // Data output
    int has_crc = dataframe_bitcount == DATAFRAME_BITCOUNT_INCL_CRC;
    int id      = (msg[ID_OFFSET] << 20) | (msg[ID_OFFSET + 1] << 12) | (msg[ID_OFFSET + 2] << 4) | (msg[ID_OFFSET + 3] >> 4);
    int token   = (msg[MESSAGE_TOKEN_OFFSET] << 8) | (msg[MESSAGE_TOKEN_OFFSET + 1]);

    int device_type = DEVICE_TYPE_UNKNOWN;
    if ((msg[COMMAND_ID_OFFSET] & 0xF) == 0x5) {
        device_type = DEVICE_TYPE_SHUTTER;
    }
    else if ((msg[COMMAND_ID_OFFSET] & 0xF) == 0xa) {
        // RojaFlex Bridge clones a remote signal but does not send an CRC!?!?
        // So we can detect if it a real remote or a bridge on the message length.
        if (has_crc) {
            device_type = DEVICE_TYPE_REMOTE;
        }
        else {
            device_type = DEVICE_TYPE_BRIDGE;
        }
    }

    char const *cmd_str = "unknown";
    switch (msg[COMMAND_ID_OFFSET]) {
    case COMMAND_ID_STOP:
        cmd_str = "Stop"; break;
    case COMMAND_ID_UP:
        cmd_str = "Up"; break;
    case COMMAND_ID_DOWN:
        cmd_str = "Down"; break;
    case COMMAND_ID_SAVE_UNSAVE_POS:
        // 5 x Stop on remote set inclined pos.
        // Command is complete identical for set and unset
        // - If nothing is saved it will set.
        // - If something is saved and the position is identical it will reset.
        //   The P2D bridge is beeping in that case.
        cmd_str = "Save/Unsave position"; break;
    case COMMAND_ID_GO_SAVED_POS:
        // Hold Stop for 5 seconds to drive to saved pos.
        cmd_str = "Go saved position"; break;
    case COMMAND_ID_REQUESTSTATUS:
        // I am not sure if that is true.
        // I know that the remote is sending the message and not the shutter.
        // I know that the bridge is not sending this message after e.g.0x1a.
        // I know that the shutter sends a Position status right after this message.
        // After the normal 0x1a command from a bridge, the position status
        // will be send wenn the stutter is completely up but not before.
        // So I think this is a "Request Shutter Status Now".
        cmd_str = "Request Status"; break;
    case 0x85: //  0%
        cmd_str = "Pos. Status 0%"; break;
    case 0x95: // 20%
        cmd_str = "Pos. Status 20%"; break;
    case 0xA5: // 40%
        cmd_str = "Pos. Status 40%"; break;
    case 0xB5: // 60%
        cmd_str = "Pos. Status 60%"; break;
    case 0xC5: // 80%
        cmd_str = "Pos. Status 80%"; break;
    case 0xD5: //100%
        cmd_str = "Pos. Status 100%"; break;
    }

    /* clang-format off */
    data = data_make(
                "model",        "Model",        DATA_COND, device_type == DEVICE_TYPE_UNKNOWN, DATA_STRING, "RojaFlex-Other",
                "model",        "Model",        DATA_COND, device_type == DEVICE_TYPE_SHUTTER, DATA_STRING, "RojaFlex-Shutter",
                "model",        "Model",        DATA_COND, device_type == DEVICE_TYPE_REMOTE, DATA_STRING, "RojaFlex-Remote",
                "model",        "Model",        DATA_COND, device_type == DEVICE_TYPE_BRIDGE, DATA_STRING, "RojaFlex-Bridge",
                "id",           "ID",           DATA_FORMAT, "%07x", DATA_INT,    id,
                "channel",      "Channel",      DATA_INT,    msg[CHANNEL_OFFSET] & 0xF,
                "token",        "Msg Token",    DATA_FORMAT, "%04x", DATA_INT,    token,
                "cmd_id",       "Value",        DATA_FORMAT, "%02x", DATA_INT,    msg[COMMAND_ID_OFFSET],
                "cmd_name",     "Command",      DATA_STRING, cmd_str,
                "cmd_value",    "Value",        DATA_INT,    msg[COMMAND_VALUE_OFFSET],
                "mic",          "Integrity",    DATA_COND,   has_crc, DATA_STRING, "CRC",
                NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

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
        uint8_t command;
        uint8_t msg_new[19];
        uint16_t sum = 0;

        decoder_log(decoder, 2, __func__, "Signal cloner");

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
                msg_new[8 + ID_OFFSET + 3] = (msg[ID_OFFSET + 3] & 0xF0) + channel;

                // Set command id + command value
                msg_new[8 + COMMAND_ID_OFFSET]    = command;
                msg_new[8 + COMMAND_VALUE_OFFSET] = 0x1;

                // Generate message token
                // TODO: This value is not completely known
                msg_new[8 + MESSAGE_TOKEN_OFFSET + 0] = (command == COMMAND_ID_REQUESTSTATUS) ? 0x02 : command;

                // Calculate sum
                sum = 0;
                for (uint8_t j=0; j <= 6; ++j) {
                    sum += msg_new[8 + ID_OFFSET + j];
                }
                msg_new[8 + MESSAGE_TOKEN_OFFSET + 1] = sum & 0xff;

                // Generate CRC
                // Thanks to: ./reveng -w 16 -s $msg1 $msg2 $msg3
                // width=16  poly=0x8005  init=0xffff  refin=false  refout=false  xorout=0x0000  check=0xaee7  residue=0x0000  name="CRC-16/CMS"
                uint16_t crc_calc                   = crc16(&msg_new[8 + LENGTH_OFFSET], 9, 0x8005, 0xffff);
                msg_new[8 + MESSAGE_CRC_OFFSET + 0] = crc_calc >> 8;
                msg_new[8 + MESSAGE_CRC_OFFSET + 1] = crc_calc & 0xFF;

                /*
                // Print final command
                */
                decoder_logf_bitrow(decoder, 2, __func__, &msg_new[0], sizeof(msg_new) * 8, "CH:%01x Command:0x%02x", channel, command);
            }

            decoder_log(decoder, 2, __func__, "");
            ++channel;
        } while ((channel <= 0xF) && GENERATE_COMMANDS_FOR_ALL_CHANNELS);
    }

    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "token",
        "cmd_id",
        "cmd_name",
        "cmd_value",
        "mic",
        NULL,
};

r_device const rojaflex = {
        .name        = "RojaFlex shutter and remote devices",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 100,
        .long_width  = 100,
        .reset_limit = 102400,
        .sync_width  = 0,
        .decode_fn   = &rojaflex_decode,
        .fields      = output_fields,
};
