/** @file
    DirecTV RC66RX Remote Control decoder.

    Copyright (C) 2019 Karl Lohner <klohner@thespill.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/**
DirecTV RC66RX Remote Control decoder.

The device uses PCM encoding with 600 μs for each raw signal bit.

A signal transmission contains a variable number of rows separated by
approximately 27,600μs of gap.

Each row is a SYNC packet (either a Long SYNC at 15 bits or a Short SYNC at
10 bits), 20 Data Packets (2, 3, or 4 bits each, deliniated by space to mark bit
transitions), and an End Of Row (EOR) packet (4 bits).

Full row is between 54 and 99 bits, but maybe typically around 65 bits (39,000μs).

E.g. rtl_433 -R 0 -X '-X n=DirecTV,m=FSK_PCM,s=600,l=600,g=30000,r=80000'

Data Row layout:

SYNC + DATA + EOR (repeated at least once, but could continue indefinitely as
long as button is held down)

SYNC is either 000111111111100 (first row in signal) or 0001111100
(subsequent rows in transmission)

DATA (20 data packets) and EOR (1 data packet) is converted to bits or EOR
marker as such:

| Raw Data Packet Bits | Decoded Logical Data Bits |
|----------------------|-------------------------- |
| 10                   | 00                        |
| 100                  | 01                        |
| 110                  | 10                        |
| 1100                 | 11                        |
| 1000                 | EOR                       |

Resulting 40 bits of data, as nibbles, decoded:

MM DD DD DB BC

| Nibble # | Letter | Description                                                                |
|----------|--------|-------------                                                               |
| 0 - 1    | MM     | Model? Seems to always be 0x10                                             |
| 2 - 6    | DDDDD  | Device ID. 0x00000 - 0xF423F are valid (000000 - 999999 in decimal)        |
| 7 - 8    | BB     | Button Code. 0x00 - 0xFF maps to specific buttons or functions             |
| 9        | C      | Checksum. Least Significant Nibble of sum of previous 9 nibbles, 0x0 - 0xF |

*/

#include "decoder.h"

#define ROW_BITLEN_MIN     54  // 48 if we received signal without leading and trailing space bits (gaps)
#define ROW_BITLEN_MAX     99
#define ROW_MINREPEATS     1
#define DTV_BITLEN_MAX  40  // Valid decoded data for this device will be 40 bits in length


static const uint8_t *dtv_button_label[] = { 
    [0x00] = "",
    [0x01] = "1",
    [0x02] = "2",
    [0x03] = "3",
    [0x04] = "4",
    [0x05] = "5",
    [0x06] = "6",
    [0x07] = "7",
    [0x08] = "8",
    [0x09] = "9",
    [0x0D] = "CH UP",
    [0x0E] = "CH DOWN",
    [0x0F] = "CH PREV",
    [0x10] = "PWR",
    [0x11] = "0",
    [0x12] = "DASH",
    [0x13] = "ENTER",
    [0x20] = "MENU",
    [0x21] = "UP",
    [0x22] = "DOWN",
    [0x23] = "LEFT",
    [0x24] = "RIGHT",
    [0x25] = "SELECT",
    [0x26] = "EXIT",
    [0x27] = "BACK",
    [0x28] = "GUIDE",
    [0x29] = "ACTIVE",
    [0x2A] = "LIST",
    [0x2E] = "INFO",
    [0x30] = "VCR PLAY",
    [0x31] = "VCR STOP",
    [0x32] = "VCR PAUSE",
    [0x33] = "VCR RWD",
    [0x34] = "VCR FFD",
    [0x35] = "VCR REC",
    [0x36] = "VCR BACK",
    [0x37] = "VCR SKIP",
    [0x41] = "RED",
    [0x42] = "YELLOW",
    [0x43] = "GREEN",
    [0x44] = "BLUE",
    [0x51] = "TV: VCR ALERT",
    [0x59] = "VOLUME ALERT",
    [0x5A] = "AV1/AV2/TV: IR ALERT 1",
    [0x5B] = "DTV: IR ALERT",
    [0x5C] = "AV1/AV2/TV: IR ALERT 2",
    [0x5D] = "TV: DTV ALERT",
    [0x5E] = "AV1: DTV ALERT",
    [0x5F] = "AV2: DTV ALERT",
    [0x73] = "FORMAT",
    [0x80] = "DTV: DTV&TV POWER ON",
    [0x81] = "DTV: DTV&TV POWER OFF",
    [0xD6] = "SELECT RELEASE"
};

const uint8_t *get_dtv_button_label(uint8_t button_id)
{
    const uint8_t *label = dtv_button_label[button_id];
    if (!label) {
        label = dtv_button_label[0];
    }
    return label;
}


static inline int bit(const uint8_t *bytes, unsigned bit)
{
    return bytes[bit >> 3] >> (7 - (bit & 7)) & 1;
}

// similar to bitbuffer_search in bitbuffer.h, but allows us to search a custom bitrow_t
unsigned bitrow_search(bitrow_t const bitrow, unsigned bit_len, unsigned start,
        const uint8_t *pattern, unsigned pattern_bits_len)
{
    unsigned ipos = start;
    unsigned ppos = 0; // cursor on init pattern

    while (ipos < bit_len && ppos < pattern_bits_len) {
        if (bit(bitrow, ipos) == bit(pattern, ppos)) {
            ppos++;
            ipos++;
            if (ppos == pattern_bits_len)
                return ipos - pattern_bits_len;
        }
        else {
            ipos -= ppos;
            ipos++;
            ppos = 0;
        }
    }

    // Not found
    return bit_len;
}

/// Set a single bit in a bitrow at bit_idx position.  Assume success, no bounds checking, so be careful!
void bitrow_set_bit(bitrow_t bitrow, unsigned bit_idx, unsigned bit_val)
{
    uint8_t bit_mask;
    if (bit_val == 0) {
        bitrow[bit_idx >> 3] &= ~(1 << (7 - (bit_idx & 7)));
    } 
    else {
        bitrow[bit_idx >> 3] |= (1 << (7 - (bit_idx & 7)));
    }
}

static int directv_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    data_t *data;
    int r;                   // a row index
    uint8_t bitrow[15];      // space for a possibly modified bitbuffer row
    uint8_t bit_len;         // row length is variable, so need to keep track of this
    uint8_t dtv[5] = {0};    // enough for exactly 40 bits of decoded data
  
    // Signal is reset by rtl_433 before recognizing rows, so in practice, there's only one row.
    // It would be useful to catch rows in signal so that there'd only be one decoded row per
    // signal and we could count repeats to report the length of the signal, but that's not
    // supported yet.  It seems the gap between rows exceeds the ook_hysteresis threshold in
    // pulse_detect.c:  int16_t const ook_hysteresis = ook_threshold / 8; // ±12%
    // and changing this value isn't the right direction, nor does this even work for this signal.
    // Grouping rows in a signal like this will need to be supported in some other way, and this 
    // support is not yet available in rtl_433.
    // For now, we'll decode the signal in the bitbuffer assuming it is only one row.
    r = 0;
    bit_len = bitbuffer->bits_per_row[r];

    if (bit_len > ROW_BITLEN_MAX) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "directv: bitbuffer row contains too many bits: %i > %i\n", bit_len, ROW_BITLEN_MAX);
        }
        return 0;
    }

    if (bit_len < ROW_BITLEN_MIN) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "directv: bitbuffer row contains too few bits: %i < %i\n", bit_len, ROW_BITLEN_MIN);
        }
        return 0;
    }

    bitbuffer_extract_bytes(bitbuffer, r, 0, bitrow, bit_len);

    // Decoder is written assuming a correctly oriented FSK buffer row, but let's be generous
    // and handle the inverse, as well as ASK/OOK reception of either the mark or space pulses.

    // TBD, try to fix up bitrow/bit_len to handle these edge cases.
    // Valid SYNCs to catch:
    // 0001111100       is normal Short SYNC
    // 000111111111100  is normal Long SYNC

    // Quick and dirty, get bitbuffer row data into a pretty string.
    uint8_t bitrow_strbuf[35];  // probably only needs 30 bytes ({xx}twenty-five-nibbles-shown\0)
    uint8_t temp_strbuf[6];     // probably only needs 5 bytes  ({xx}\0)
    sprintf(bitrow_strbuf, "{%2d}", bit_len);
    for (unsigned col = 0; col < (bit_len + 7) / 8; ++col) {
        sprintf(temp_strbuf, "%02x", bitrow[col]);
        strcat( bitrow_strbuf, temp_strbuf );
    }
    if (decoder->verbose > 2) {
        fprintf(stderr, "directv: rowbuf bit value is: %s.\n", bitrow_strbuf);
    }

    // Look for a Long or Short SYNC at the start of bitrow
    uint8_t sync[1];
    sync[0]=0xf8;
    uint8_t sync_len=7;
    uint8_t sync_type;
    sync_type = 0;  // sync_type 0 is a Short SYNC, 1 is a Long SYNC
    unsigned bit_idx;

    bit_idx = bitrow_search(bitrow, bit_len, 0, sync, sync_len);
    
    if (bit_idx == bit_len) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "directv: bitbuffer row does not contain SYNC\n");
        }
        return 0;
    }

    if (bit_idx > 0 && bitrow_get_bit(bitrow, bit_idx-1) == 1) {
        sync_type=1;
    }
    
    bit_idx += sync_len;

    // SYNC found, start decoding to logical bits int dtv
    // DTV_BITLEN_MAX is 40

    unsigned dtv_bit_idx = 0; // keep track of where we are in the decoded bit array
    uint8_t prev_bit=0;   // we'll need to keep track of the previous bitrow bit
    uint8_t dtv_twobits;  // we decode two logical bits at a time

    // Skip by any leading zeros.  There shouldn't be any anyway.
    while (bit_idx < bit_len) {
        if(bitrow_get_bit(bitrow, bit_idx++)==1) {
            break;
        }
    }
    dtv_twobits = 0;
    // bit_idx is just past the first 1 in the bitrow or at the end of bitrow if no 1's
    while (bit_idx < bit_len) {
        // will be either 10 or 11 at this point, both OK, just make that 0 or 1 bit our result in progress
        dtv_twobits += bitrow_get_bit(bitrow, bit_idx++);
        // will be either 100 or 110 or 101 or 111 at this point, or at the end of bitrow
        if (bit_idx < bit_len) {
            // will be either 100 or 110 or 101 or 111 at this point
            if (bitrow_get_bit(bitrow, bit_idx++) == 1) {
                // got a valid 10 unit for 00 value and start of next unit, or a strange 111 unit
                if (dtv_twobits > 0) {  // got a strange 111 unit
                    if (decoder->verbose > 1) {
                        fprintf(stderr, "directv: bitbuffer contains invalid 111 bits after SYNC\n");
                    }
                    return 0;
                }
                // got a valid 10 unit for 00 value and a 1 to start the next unit
                // push our logical bit decoding into dtv
                if (dtv_bit_idx < (DTV_BITLEN_MAX - 1)) {
                    dtv[dtv_bit_idx >> 3] |= (dtv_twobits << (6 - (dtv_bit_idx & 7)));
                    dtv_bit_idx += 2;
                    dtv_twobits = 0;
                    continue;
                }
                // We received too much data (more than DTV_BITLEN_MAX bit of decoded data!)
                if (decoder->verbose > 1) {
                    fprintf(stderr, "directv: bitbuffer contains unexpected 1 bits after all expected bits received.\n");
                }
                return 0;
            }
            // will be either 1001 or 1101 or 1000 or 1100 at this point, or end of the bitrow
            if (bit_idx < bit_len) {
                // will be either 1001 or 1101 or 1000 or 1100 at this point
                if (bitrow_get_bit(bitrow, bit_idx++) == 1) {
                    // got either a valid 100 or 110 unit and the start of the next unit
                    dtv_twobits++;  // 100 becomes 1 and 110 becomes 2
                    // push our logical bit decoding into dtv
                    if (dtv_bit_idx < (DTV_BITLEN_MAX - 1)) {
                        dtv[dtv_bit_idx >> 3] |= (dtv_twobits << (6 - (dtv_bit_idx & 7)));
                        dtv_bit_idx += 2;
                        dtv_twobits = 0;
                        continue;
                    }
                    // We received too much data (more than DTV_BITLEN_MAX bit of decoded data!)
                    if (decoder->verbose > 1) {
                        fprintf(stderr, "directv: bitbuffer contains unexpected bits after all expected bits received.\n");
                    }
                    return 0;
                }
                // will be either 10001 or 11001 or 10000 or 11000 or at end of bitrow at this point
                if (bit_idx < bit_len) {
                    // will be either 10001 or 11001 or 10000 or 11000 at this point
                    if (bitrow_get_bit(bitrow, bit_idx++) == 1) {
                        // got either a valid 1100 unit and the start of the next unit, or invalid 10001 (data after EOR?)
                        if (dtv_twobits > 0) {
                            // Got valid 1100
                            dtv_twobits = 3;
                            // push our logical bit decoding into dtv
                            if (dtv_bit_idx < (DTV_BITLEN_MAX - 1)) {
                                dtv[dtv_bit_idx >> 3] |= (dtv_twobits << (6 - (dtv_bit_idx & 7)));
                                dtv_bit_idx += 2;
                                dtv_twobits = 0;
                                continue;
                            }
                            // We received too much data (more than DTV_BITLEN_MAX bit of decoded data!)
                            if (decoder->verbose > 1) {
                                fprintf(stderr, "directv: bitbuffer contains unexpected bits after all expected bits received.\n");
                            }
                            return 0;
                        }
                        // Got invalid 10001 (data after EOR?)
                        if (dtv_bit_idx < (DTV_BITLEN_MAX - 1)) {
                            // We received an EOR and was expecting more data
                            if (decoder->verbose > 1) {
                                fprintf(stderr, "directv: bitbuffer contains 1000 bits (EOR) before end of expected bits.\n");
                            }
                            return 0;
                        }
                        if (decoder->verbose > 1) {
                            fprintf(stderr, "directv: bitbuffer contains data after the expected End Of Record marker.\n");
                        }
                        return 0;
                    }
                    // will be either 10000 or 11000 at end of bitrow at this point
                    // 10000 might be OK if no more 1's until end of row, but 11000 is completely unexpected.
                    if (dtv_twobits > 0) {
                        // 11000 is not OK.
                        if (decoder->verbose > 1) {
                            fprintf(stderr, "directv: bitbuffer contains unexpected 11000 bits.\n");
                        }
                        return 0;
                    }
                    // 10000 is OK if no more 1's found before end of bitrow
                    while (bit_idx < bit_len) {
                        dtv_twobits += bitrow_get_bit(bitrow, bit_idx++);
                    }
                    if (dtv_twobits > 0) {
                        // We received unexpected 1 bits after an EOR
                        // Did we expect the EOR here or not, though?
                        if (dtv_bit_idx < (DTV_BITLEN_MAX - 1)) {
                            if (decoder->verbose > 1) {
                                fprintf(stderr, "directv: bitbuffer contains unexpected bits after unexpected End Of Row bits.\n");
                            }
                            return 0;
                        }
                        if (decoder->verbose > 1) {
                            fprintf(stderr, "directv: bitbuffer contains unexpected bits after the expected End Of Row bits.\n");
                        }
                        return 0;
                    }
                    // End of bitrow and EOR just had some trailing 0 bits.
                    // Did we expect the EOR here or not, though?
                    if (dtv_bit_idx < (DTV_BITLEN_MAX - 1)) {
                        if (decoder->verbose > 1) {
                            fprintf(stderr, "directv: bitbuffer contains unexpected End Of Row bits before we received complete data.\n");
                        }
                        return 0;
                    }
                    // Everything is good enough if we just ignore the trailing 0 bits.
                    break;
                }
                // End of bitrow and either 1000 or 1100 received. 1000 is probably expected. 1100 is definitely bad.
                if (dtv_twobits > 0) {
                    // We shouldn't end bitrow with a 1100 data unit
                    if (decoder->verbose > 1) {
                        fprintf(stderr, "directv: bitbuffer contains unexpected 1100 bits at end of bitrow.\n");
                    }
                    return 0;
                }
                // 1000 is EOR and is expected at end of bitrow, but did we fill up dtv yet?
                if (dtv_bit_idx < (DTV_BITLEN_MAX - 1)) {
                    if (decoder->verbose > 1) {
                        fprintf(stderr, "directv: bitbuffer contains End Of Row marker but not enough row data provided (%d < %d).\n", dtv_bit_idx, DTV_BITLEN_MAX );
                    }
                    return 0;
                }
                // Great!  We got the 1000 EOR exactly where we expected it.
                break;
            }
            // will be either 100 or 110 at end of the bitrow here.  Not good.
            if (decoder->verbose > 1) {
                fprintf(stderr, "directv: bitbuffer contains unexpected %s bits at end of bitrow.\n", dtv_twobits==0?"100":"110");
            }
            return 0;
        }
        // will be either 10 or 11 at the end of bitrow here.  Not good.
        if (decoder->verbose > 1) {
            fprintf(stderr, "directv: bitbuffer contains unexpected %s bits at end of bitrow.\n", dtv_twobits==0?"10":"11");
        }
        return 0;
    }
    // We've received an acceptable decoded dtv value here.

    // Quick and dirty, get bitbuffer row data into a pretty string.
    uint8_t dtv_strbuf[35] = {0};  // probably only needs 25 bytes ({xx}twenty-nibbles-shown\0)
    sprintf(dtv_strbuf, "{%2d}", DTV_BITLEN_MAX);
    for (unsigned col = 0; col < (DTV_BITLEN_MAX + 7) / 8; ++col) {
        sprintf(temp_strbuf, "%02x", dtv[col]);
        strcat( dtv_strbuf, temp_strbuf );
    }
    if (decoder->verbose > 2) {
        fprintf(stderr, "directv: decoded data value is: %s.\n", dtv_strbuf);
    }

    // Break apart dtv data into fields and validate
    
    // First byte should be 0x10 (magic number?)
    if (dtv[0] != 0x10) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "directv: Bad magic number: 0x%02x should be 0x10.\n", dtv[0]);
        }
        return 0;
    }

    // Validate Checksum
    if ((dtv[0] >> 4 + dtv[0] & 0x0F + dtv[1] >> 4 + dtv[1] & 0x0F + dtv[2] >> 4 + dtv[2] & 0x0F + 
        dtv[3] >> 4 + dtv[3] & 0x0F + dtv[4] >> 4) & 0x0F != dtv[4] & 0x0F)
    {
        if (decoder->verbose > 1) {
            fprintf(stderr, "directv: Checksum failed.\n");
        }
        return 0;
    }
    
    // Get Device ID 
    unsigned dtv_device_id;
    dtv_device_id = dtv[1] << 12 | dtv[2] << 4 | dtv[3] >> 4;
    if (dtv_device_id > 999999) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "directv: Bad Device ID: %d.\n", dtv_device_id);
        }
        return 0;
    }

    // Get Button ID
    uint8_t dtv_button_id;
    dtv_button_id = dtv[3] << 4 | dtv[4] >> 4;
    // Assume all byte values are valid.

    data = data_make(
            "model",         "",            DATA_STRING, "DirecTV-RC66RX",
            "device_id",     "",            DATA_FORMAT, "%06d", DATA_INT, dtv_device_id,
            "button_id",     "",            DATA_FORMAT, "%d", DATA_INT, dtv_button_id,
            "button_name",   "",            DATA_FORMAT, "[%s]", DATA_STRING, get_dtv_button_label(dtv_button_id),
            "event",         "",            DATA_STRING, sync_type==0?"REPEAT":"INITIAL",
            "mic",           "",            DATA_STRING, "CHECKSUM",
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "device_id",
    "button_id",
    "button_name",
    "event",
    "mic",
    NULL
};

r_device directv = {
        .name        = "DirecTV RC66RX Remote Control",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 600,  // 150 samples @250k
        .long_width  = 600,  // 150 samples @250k
        .gap_limit   = 30000, // gap is typically around 27,600μs, so long that rtl_433 resets 
                              // signal decoder before recognizing row repeats in signal
        .reset_limit = 50000, // maximum gap size before End Of Row [μs]
        .decode_fn   = &directv_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
