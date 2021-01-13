/** @file
    Cavius decoder.

 
*/

#include "decoder.h"

// typedef struct {
//     uint32_t net_id;
//     uint8_t  message;
//     uint8_t  message_i;
//     uint8_t  crc;
//     uint32_t sender_id;
// } cavius_packet_t;

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

    bitbuffer_print(&databits);

    uint32_t net_id    = 0;
    uint8_t  message   = 0;
    uint8_t  message_i = 0;
    uint8_t  crc       = 0;
    uint32_t sender_id = 0;

    bitbuffer_extract_bytes(&databits, 0, 0*8, &net_id,    4*8);
    bitbuffer_extract_bytes(&databits, 0, 4*8, &message,   1*8);
    bitbuffer_extract_bytes(&databits, 0, 5*8, &message_i, 1*8);
    bitbuffer_extract_bytes(&databits, 0, 6*8, &crc,       1*8);
    bitbuffer_extract_bytes(&databits, 0, 7*8, &sender_id, 4*8);

    // Some uglyish bit banging here. Extraction seems to change endianness?
    uint32_t net_id_big = ((net_id>>24)&0xff) | // move byte 3 to byte 0
                          ((net_id<<8)&0xff0000) | // move byte 1 to byte 2
                          ((net_id>>8)&0xff00) | // move byte 2 to byte 1
                          ((net_id<<24)&0xff000000); // byte 0 to byte 3

    uint32_t sender_id_big = ((sender_id>>24)&0xff) | // move byte 3 to byte 0
                             ((sender_id<<8)&0xff0000) | // move byte 1 to byte 2
                             ((sender_id>>8)&0xff00) | // move byte 2 to byte 1
                             ((sender_id<<24)&0xff000000); // byte 0 to byte 3

    printf("Net: %d Sender: %d Message %02x\n", net_id_big, sender_id, message);

    /* clang-format off */
    data = data_make(
            "netid",         "Net ID",      DATA_INT,    net_id_big,
            "senderid",      "Sender ID",   DATA_INT,    sender_id_big,
            "message",       "Message",     DATA_INT,    message,
            "crc",           "CRC-8",       DATA_INT,    crc,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "netid",
        "senderid",
        "message",
        "crc",
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
