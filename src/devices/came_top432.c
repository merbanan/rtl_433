/** @file
    Decode CAME remote control TOP-432EV, TOP-432NA, TOP-432EE.
    This remote control is used for garage door and sliding gate. It transmits on 433.92 MHz (as it is written on the case), built since 2006 
    (as said on the FCC site https://www.fcc.gov/oet/ea/fccid with reference M48 TOP-NA)

    It works with CAME radio receiver cards "AF43S", capable of handling 4096 codes. 
    CAME is an italian company. Theses remote controls are mainly sold in europe (France, Italy, Belgium). https://www.came.com and https://www.came-europe.com .

    Copyright (C) 2020 Benjamin Larsson

 */

/**
The device uses PPM encoding,
- 0 is encoded as 320 us gap and 640 us pulse,
- 1 is encoded as 640 us gap and 320 us pulse.
The device sends a 4 times the packet when a button on the remote control is pressed.
A transmission starts with a 320 us pulse. At the end of the packet, there is a minimum of 36 periods of 320us between messages (11520us)

Diagram :
          |        <---> logical 0          <---> logical 1                     <-- 11520us -->
          |       _   __    _   __   __   __    _    _    _   __   __    _    _                 _
Pulse/gap | _____| |_|  |__| |_|  |_|  |_|  |__| |__| |__| |_|  |_|  |__| |__| |_____(..)______| |
          +------------------------------------------------------------------------------   
bits              ~~ : start bit (320us)
                    |  0| 1  |  0 |  0 |  0 |  1 |  1 |  1 |  0 |  0 |  1 |  1 |
                    |        0x4       |       0x7         |       0x3         |

Data layout:
    ccc
- c : 4-bit data

There is no CRC, no parity, no preamble (only the start pulse indicate the begining of packet)
*/
#include "decoder.h"

#define CAMETOP432_BITLEN      12

static int came_top432_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int r; // a row index
    uint8_t *b; // bits of a row
    int parity;
    uint8_t r_crc, c_crc;
    uint16_t sensor_id;
    uint8_t msg_type;
    int16_t code;
    char code_str[3];
    
    if (decoder->verbose > 1) {
        bitbuffer_printf(bitbuffer, "%s: ", __func__);
    }

    for (r = 0; r < bitbuffer->num_rows; ++r) {
        b = bitbuffer->bb[r];

        /*
         * Validate message and reject invalid messages as
         * early as possible before attempting to parse data.
         *
         * Check "message envelope"
         * - valid message length (use a minimum length to account
         *   for stray bits appended or prepended by the demod)
         * - valid preamble/device type/fixed bits if any
         * - Data integrity checks (CRC/Checksum/Parity)
         */

        if (bitbuffer->bits_per_row[r] < CAMETOP432_BITLEN) {
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s: bitbuffer len received %d, expected : %d\n", __func__, bitbuffer->bits_per_row[r], CAMETOP432_BITLEN);
            }
            return DECODE_ABORT_EARLY;
        }
        // no preamble
        // no crc
        // no checksum
        // no parity
    }

    // packet is not repeated
    b = bitbuffer->bb[0];

    // reconstruct the 12 bits code
    code = ((b[0] << 4) | (b[1]>>4)) & 0xFFF;
    sprintf(code_str, "%03x", code);
    /* clang-format off */
    data = data_make(
            "model",    "", DATA_STRING,   "CAME-TOP432",
            "code",     "", DATA_INT,      code,
            "code_hex", "", DATA_STRING,   code_str,
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

    // Return 1 if message successfully decoded
    return 1;
}

/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */
static char *output_fields[] = {
        "model",
        "code",
        "code_hex",
        NULL,
};

r_device came_top432 = {
        .name        = "Came TOP432 remote control",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 320,  // 
        .long_width  = 640,  // 
        .sync_width  = 320,
        //.gap_limit   = 0,  // dont know how to find this value
        .reset_limit = 36*320, // a bit longer than packet gap
        .decode_fn   = &came_top432_decode,
        .disabled    = 1, // disabled and hidden by default (because there is no crc/preamble on this protocol)
        .fields      = output_fields,
};
