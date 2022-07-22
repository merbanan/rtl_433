/** @file
    raspi.c
    Raspberry Pi flexible-payload remote sensor decoder

    Copyright (C) 2022 H David Todd

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/*
    Use this as a starting point for a new decoder.

    Keep the Doxygen (slash-star-star) comment above to document the file and copyright.

    Keep the Doxygen (slash-star-star) comment below to describe the decoder.
    See http://www.doxygen.nl/manual/markdown.html for the formatting options.

    Remove all other multiline (slash-star) comments.
    Use single-line (slash-slash) comments to annontate important lines if needed.

    To use this:
    - Copy this template to a new file
    - Change at least `new_template` in the source
    - Add to include/rtl_433_devices.h
    - Run ./maintainer_update.py (needs a clean git stage or commit)

    Note that for simple devices doorbell/PIR/remotes a flex conf (see conf dir) is preferred.
*/

/**
This is a multi-purpose decoder for remote-sensor data packets sent
from a Raspberry Pi.  The data packet header includes an option
field that allows up to 16 different interpretations for the payload
data. This decoder works in coordination with the Raspberry Pi transmitter
code to interpret the data that has been sent.  See  
   https://github.com/hdtodd/Emulator-433
for example code for the transmitter software.

The device uses PPM encoding modeled after the Acurite 609TX::
- short pulse is 232 us
- long pulse  is 420 us
- gap         is 384 us
The decoder expects at least 3 repeated packets; the example 
code sends 5.

There is no preamble in the packet, but the packet has a 1-byte
header with a 4-bit type code and a 4-bit channel code.  The
decoder can process packets from up to 16 different remote
Raspberry Pi sensors, each with up to 16 different types of 
payload data.

The packet includes an appended CRC8 byte for checksum that uses 
0x97 as the polynomial divisor and 0x00 as the initial value.
See http://users.ece.cmu.edu/~koopman/pubs/KoopmanCRCWebinar9May2012.pdf
for analysis and https://github.com/hdtodd/CRC8Libary for code
and testing/validation programs.

The total packet length is 80 bits (10 bytes) with 1 byte header,
1 byte CRC and 8 bytes/64 bits of flexibly-formatted payload data.

Data layout as nibbles:
    TN DD DD DD DD DD DD DD DD CC
- T: 4-bit type code
- N: 4-bit channel number
- D: 16 4-bit data nibbles (8 bytes total)
- C: 8-bit (1 byte) CRC8 checksum code of first 9 bytes (poly 0x97, init 0x00)

*/

#include "decoder.h"

/**
 * Raspberry Pi multi-function remote sensor device
 *
 * Message is 80 bits long
 * Messages start with 4-bit type & 4-bit channel
 * Data payload is 8 bytes, interpreted according to message type
 * Messages end with 1-byte CRC8 checksum of first 9 bytes
 * The message is repeated as 5 packets by the transmitter.
 * Decoder requires reception of at least 3 repeated packets to accept.
 *
 */

#define MYDEVICE_BITLEN     80
#define MYDEVICE_CRC_INIT   0x00
#define MYDEVICE_MINREPEATS 2

// table created with polynomial 0x97 for CRC-8 division
static const uint8_t CRC8Table[256] = {
	0x00, 0x97, 0xb9, 0x2e, 0xe5, 0x72, 0x5c, 0xcb, 
	0x5d, 0xca, 0xe4, 0x73, 0xb8, 0x2f, 0x01, 0x96, 
	0xba, 0x2d, 0x03, 0x94, 0x5f, 0xc8, 0xe6, 0x71, 
	0xe7, 0x70, 0x5e, 0xc9, 0x02, 0x95, 0xbb, 0x2c, 
	0xe3, 0x74, 0x5a, 0xcd, 0x06, 0x91, 0xbf, 0x28, 
	0xbe, 0x29, 0x07, 0x90, 0x5b, 0xcc, 0xe2, 0x75, 
	0x59, 0xce, 0xe0, 0x77, 0xbc, 0x2b, 0x05, 0x92, 
	0x04, 0x93, 0xbd, 0x2a, 0xe1, 0x76, 0x58, 0xcf, 
	0x51, 0xc6, 0xe8, 0x7f, 0xb4, 0x23, 0x0d, 0x9a, 
	0x0c, 0x9b, 0xb5, 0x22, 0xe9, 0x7e, 0x50, 0xc7, 
	0xeb, 0x7c, 0x52, 0xc5, 0x0e, 0x99, 0xb7, 0x20, 
	0xb6, 0x21, 0x0f, 0x98, 0x53, 0xc4, 0xea, 0x7d, 
	0xb2, 0x25, 0x0b, 0x9c, 0x57, 0xc0, 0xee, 0x79, 
	0xef, 0x78, 0x56, 0xc1, 0x0a, 0x9d, 0xb3, 0x24, 
	0x08, 0x9f, 0xb1, 0x26, 0xed, 0x7a, 0x54, 0xc3, 
	0x55, 0xc2, 0xec, 0x7b, 0xb0, 0x27, 0x09, 0x9e, 
	0xa2, 0x35, 0x1b, 0x8c, 0x47, 0xd0, 0xfe, 0x69, 
	0xff, 0x68, 0x46, 0xd1, 0x1a, 0x8d, 0xa3, 0x34, 
	0x18, 0x8f, 0xa1, 0x36, 0xfd, 0x6a, 0x44, 0xd3, 
	0x45, 0xd2, 0xfc, 0x6b, 0xa0, 0x37, 0x19, 0x8e, 
	0x41, 0xd6, 0xf8, 0x6f, 0xa4, 0x33, 0x1d, 0x8a, 
	0x1c, 0x8b, 0xa5, 0x32, 0xf9, 0x6e, 0x40, 0xd7, 
	0xfb, 0x6c, 0x42, 0xd5, 0x1e, 0x89, 0xa7, 0x30, 
	0xa6, 0x31, 0x1f, 0x88, 0x43, 0xd4, 0xfa, 0x6d, 
	0xf3, 0x64, 0x4a, 0xdd, 0x16, 0x81, 0xaf, 0x38, 
	0xae, 0x39, 0x17, 0x80, 0x4b, 0xdc, 0xf2, 0x65, 
	0x49, 0xde, 0xf0, 0x67, 0xac, 0x3b, 0x15, 0x82, 
	0x14, 0x83, 0xad, 0x3a, 0xf1, 0x66, 0x48, 0xdf, 
	0x10, 0x87, 0xa9, 0x3e, 0xf5, 0x62, 0x4c, 0xdb, 
	0x4d, 0xda, 0xf4, 0x63, 0xa8, 0x3f, 0x11, 0x86, 
	0xaa, 0x3d, 0x13, 0x84, 0x4f, 0xd8, 0xf6, 0x61, 
	0xf7, 0x60, 0x4e, 0xd9, 0x12, 0x85, 0xab, 0x3c
};

static uint8_t mycrc8(uint8_t *msg, int lengthOfMsg, uint8_t init) {
  while (lengthOfMsg-- > 0) init = CRC8Table[ (init ^ *msg++)];
  return init;
}

static int raspi_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    //    uint8_t *b; // bits of a row
    uint8_t *bb = NULL;
    int brow;   // a row index
    uint8_t r_crc=0, c_crc;
    uint8_t msg_type, dev_id;
    int16_t value=999;
    char dumpstr[100];
    
    /*
     * Early debugging aid to see demodulated bits in buffer and
     * to determine if your limit settings are matched and firing
     * this decode callback.
     *
     * 1. Enable with -vvv (debug decoders)
     * 2. Delete this block when your decoder is working
     */
    //    if (decoder->verbose > 1) {
    //        bitbuffer_printf(bitbuffer, "%s: ", __func__);
    //    }

    /*
     * If you expect the bits flipped with respect to the demod
     * invert the whole bit buffer.
     */

            bitbuffer_invert(bitbuffer);

    /*
     * The bit buffer will contain multiple rows.
     * Typically a complete message will be contained in a single
     * row if long and reset limits are set correctly.
     * May contain multiple message repeats.
     * Message might not appear in row 0, if protocol uses
     * start/preamble periods of different lengths.
     */

    /*
     * Or, if you expect repeated packets
     * find a suitable row:
     */

    brow = bitbuffer_find_repeated_row(bitbuffer, MYDEVICE_MINREPEATS, MYDEVICE_BITLEN);
    if (brow < 0 || bitbuffer->bits_per_row[brow] > MYDEVICE_BITLEN + 16) {
        return 0;
    }

    bb = bitbuffer->bb[brow];

    /*
     * Check message integrity with CRC-8
     * CRC over all bytes in message should = 0
     */
    //    c_crc = crc8(bb, MYDEVICE_BITLEN / 8, 0x97, MYDEVICE_CRC_INIT);
    c_crc = mycrc8(bb, MYDEVICE_BITLEN / 8, MYDEVICE_CRC_INIT);
    if (c_crc != 0x00) {
        // Enable with -vv (verbose decoders)
        if (decoder->verbose) {
            fprintf(stderr, "%s: bad CRC: calculated %02x, received %02x\n",
                    __func__, c_crc, r_crc);
        }

        // reject row
        return 0;
    }

    printf("\n\n!\nValid packet recognized in raspi\n!\n\n");
    
    /*
     * Message "envelope" has been validated; parse data.
     */
    msg_type  = (bb[0]&0xf0)>>4;
    dev_id    = (bb[0]&0x0f);

    switch (msg_type) {
    case 0x0f:      // dump_data: simple debugging case: dump in hex
    /* clang-format off */
      sprintf(dumpstr, "0x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
	      bb[0],bb[1],bb[2],bb[3],bb[4],bb[5],bb[6],bb[7],bb[8],bb[9]);
      data = data_make(
            "model", "", DATA_STRING, "raspi",
            "id",    "", DATA_INT,    dev_id,
            "data",  "", DATA_STRING, dumpstr,
            "mic",   "", DATA_STRING, "CRC",   // CRC, CHECKSUM, or PARITY
            NULL);
    /* clang-format on */
	decoder_output_data(decoder, data);
        break;
    case 0x01:     // pi_health: simple report of this Pi's health
      value = bb[1];
      data = data_make(
            "model", "", DATA_STRING, "raspi",
            "id",    "", DATA_INT,    dev_id,
            "data",  "", DATA_INT,    value,
            "mic",   "", DATA_STRING, "CRC",   // CRC, CHECKSUM, or PARITY
            NULL);
    /* clang-format on */
	decoder_output_data(decoder, data);
        break;
    case 0x02:  // probe_data: report of temperature probe
        data = data_make(
            "model", "", DATA_STRING, "raspi",
            "id",    "", DATA_INT,    dev_id,
            "data",  "", DATA_INT,    value,
            "mic",   "", DATA_STRING, "CRC",   // CRC, CHECKSUM, or PARITY
            NULL);
    /* clang-format on */
	decoder_output_data(decoder, data);
        break;
    case 3:
    default: return 0;      // unrecognized type
    }

    /* clang-format off */
    //    data = data_make(
    //      "model", "", DATA_STRING, "raspi",
    //      "id",    "", DATA_INT,    dev_id,
    //      "data",  "", DATA_INT,    value,
    //      "mic",   "", DATA_STRING, "CRC",   // CRC, CHECKSUM, or PARITY
    //      NULL);
    /* clang-format on */
    //    decoder_output_data(decoder, data);

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
static char *raspi_output_fields[] = {
        "msg_type",
        "dev_id",
        "counter",
        "cs",
        NULL,
};

/*
 * r_device - registers device/callback. see rtl_433_devices.h
 *
 * Timings:
 *
 * short, long, and reset - specify pulse/period timings in [us].
 *     These timings will determine if the received pulses
 *     match, so your callback will fire after demodulation.
 *
 * Modulation:
 *
 * The function used to turn the received signal into bits.
 * See:
 * - pulse_demod.h for descriptions
 * - r_device.h for the list of defined names
 *
 * This device is disabled and hidden, it can not be enabled.
 *
 * To enable your device, append it to the list in include/rtl_433_devices.h
 * and sort it into src/CMakeLists.txt or run ./maintainer_update.py
 *
 */
r_device raspi  = {
        .name        = "RasPi decoder",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 1040,  // short gap is 500 us
        .long_width  = 2060, // long gap is 420 us
        .gap_limit   = 3000, // long gap 384 us, sync gap 592 us
        .reset_limit = 10000, // no packet gap, sync gap is 592 us
		.sync_width  = 5000,  // sync pulse is 632 us
        .decode_fn   = &raspi_decode,
        .fields      = raspi_output_fields,
};
