/** @file
    DirecTV RC66RX Remote Control decoder.

    Copyright (C) 2019 Karl Lohner <klohner@thespill.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

/** @fn int directv_decode(r_device *decoder, bitbuffer_t *bitbuffer)
DirecTV RC66RX Remote Control decoder.

The device uses FSK to transmit a PCM signal TRANSMISSION.  Its FSK signal
seems to be centered around 433.92 MHz with its MARK and SPACE frequencies
each +/- 50 kHz from that center point.

A full signal TRANSMISSION consists of ROWS, which are collections of SYMBOLS.
SYMBOLS, both the higher-frequency MARK (`1`) and lower-frequency SPACE
(`0`), have a width of 600μs.  If there is more than one ROW in a single
TRANSMISSION, there will be a GAP of 27,600μs of silence between each ROW.

A TRANSMISSION may be generated in response to an EVENT on the remote.  Observed
EVENTS that may trigger a TRANSMISSION seem limited to manual button presses.

Each ROW in the TRANSMISSION consists of two ordered parts -- its SYNC and its
MESSAGE.  Each ROW is expected to be complete; the device does not seem to ever
truncate a signal inside of a ROW.

The SYNC may be either a LONG SYNC or a SHORT SYNC. The LONG SYNC consists of
SYMBOLS `000111111111100`.  It is used in each row to signify that the MESSAGE
which follows will be the first time this unique MESSAGE will be seen in
this TRANSMISSION.

However, if a unique MESSAGE is to be sent more than once in a
TRANSMISSION, each subsequent ROW with this repeated MESSAGE will send a
SHORT SYNC instead of a LONG SYNC.  A SHORT SYNC consists of SYMBOLS
`0001111100`.

ROWS are typically repeated for the duration of the EVENT (a button push on the
remote) and a ROW is allowed to finish sending even if the EVENT ends before the
ROW is completely sent.

ROWS in any single TRANSMISSION usually contain the same MESSAGE, however this
is not always the case.  TRANSMISSIONS may be one ROW for some short EVENTS,
although some specific EVENTS generate TRANSMISSIONS of three rows, regardless
the duration of the EVENT.  Single TRANSMISSIONS have been observed to switch
from one MESSAGE to another.  This seems to happen for specific buttons, such as
the [SELECT] button, which sends a single ROW containing a LONG SYNC and a
MESSAGE that encodes a new [SELECT RELEASE] MESSAGE.  Some buttons send one
MESSAGE during the initial duration of the EVENT, but then switch to a new
MESSAGE if the EVENT continues. Some TRANSMISSIONS stop sending ROWS after a
duration even if the EVENT continues.

LOGICAL DATA in the MESSAGE may be decoded from the ROW using some sort of
Differential Pulse Width Modulation (DPWM) method.  Between each SYMBOL
transition (both `1` to `0` and `0` to `1`) consider the number of SYMBOLS.  If
there is only one SYMBOL, the LOGICAL DATA bit is a `0`.  If there are two
SYMBOLS, the LOGICAL DATA bit is a `1`.  If there are 3 or more SYMBOLS, this is
not DATA - it is a sync pulse.  If a sync pulse is found (and is followed by
more SYMBOLS i.e. the SYMBOL does not occur at the end of the ROW), both it and
the one or two contiguous SYMBOLS after it are ignored and LOGICAL DATA would
resume decoding from that next transition.

After decoding, there should be 40 bits (5 bytes) of LOGICAL DATA.

LOGICAL DATA layout in nibbles:

MM DD DD DB BC

| Nibble # | Letter | Description                                                                |
|----------|--------|-------------                                                               |
| 0 - 1    | MM     | Model? Seems to always be 0x10                                             |
| 2 - 6    | DDDDD  | Device ID. 0x00000 - 0xF423F are valid (000000 - 999999 in decimal)        |
| 7 - 8    | BB     | Button Code. 0x00 - 0xFF maps to specific buttons or functions             |
| 9        | C      | Checksum. Least Significant Nibble of sum of previous 9 nibbles, 0x0 - 0xF |

Flex Spec to get ROW SYMBOLS:

$ rtl_433 -R 0 -X '-X n=DirecTV,m=FSK_PCM,s=600,l=600,g=30000,r=80000'

*/

#include "decoder.h"

#define ROW_BITLEN_MIN     44  // The shortest possible fragment that can possibly decode successfully
#define ROW_BITLEN_MAX     99  // But even with a LONG SYNC and large MESSAGE value, won't be larger than this
#define ROW_MINREPEATS     1   // We're expecting that all bitbuffers coming here only have one row at a time
#define ROW_SYNC_SHORT_LEN 5   // A SYNC longer than this will be considered a LONG SYNC
#define DTV_BITLEN_MAX     40  // Valid decoded data for this device will be exactly 40 bits in length

// Provide a lookup between button ID codes and their names based on observations
static const char *dtv_button_label[] = {
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
    [0x14] = "DASH REPEAT",
    [0x15] = "ENTER REPEAT",
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
    [0x2B] = "LIST REPEAT",
    [0x2C] = "INFO REPEAT",
    [0x2D] = "GUIDE REPEAT",
    [0x2E] = "INFO",
    [0x30] = "VCR PLAY",
    [0x31] = "VCR STOP",
    [0x32] = "VCR PAUSE",
    [0x33] = "VCR RWD",
    [0x34] = "VCR FFD",
    [0x35] = "VCR REC",
    [0x36] = "VCR BACK",
    [0x37] = "VCR SKIP",
    [0x38] = "VCR SKIP REPEAT",
    [0x3A] = "VCR PLAY REPEAT",
    [0x3B] = "VCR PAUSE REPEAT",
    [0x3C] = "VCR RWD REPEAT",
    [0x3D] = "VCR FFD REPEAT",
    [0x3E] = "VCR REC REPEAT",
    [0x3F] = "VCR BACK REPEAT",
    [0x41] = "RED",
    [0x42] = "YELLOW",
    [0x43] = "GREEN",
    [0x44] = "BLUE",
    [0x45] = "MENU REPEAT",
    [0x46] = "ACTIVE REPEAT",
    [0x4A] = "RED REPEAT",
    [0x4B] = "YELLOW REPEAT",
    [0x4C] = "GREEN REPEAT",
    [0x4D] = "BLUE REPEAT",
    [0x51] = "TV: VCR ALERT",
    [0x59] = "VOLUME ALERT",
    [0x5A] = "AV1/AV2/TV: IR ALERT 1",
    [0x5B] = "DTV: IR ALERT",
    [0x5C] = "AV1/AV2/TV: IR ALERT 2",
    [0x5D] = "TV: DTV ALERT",
    [0x5E] = "AV1: DTV ALERT",
    [0x5F] = "AV2: DTV ALERT",
    [0x60] = "0 REPEAT",
    [0x61] = "1 REPEAT",
    [0x62] = "2 REPEAT",
    [0x63] = "3 REPEAT",
    [0x64] = "4 REPEAT",
    [0x65] = "5 REPEAT",
    [0x66] = "6 REPEAT",
    [0x67] = "7 REPEAT",
    [0x68] = "8 REPEAT",
    [0x69] = "9 REPEAT",
    [0x73] = "FORMAT",
    [0x75] = "FORMAT REPEAT",
    [0x80] = "DTV: DTV&TV POWER ON",
    [0x81] = "DTV: DTV&TV POWER OFF",
    [0xD6] = "SELECT RELEASE",
    [0x100] = "unknown",
};

const char *get_dtv_button_label(uint8_t button_id)
{
    const char *label = dtv_button_label[button_id];
    if (!label) {
        label = dtv_button_label[0x100];
    }
    return label;
}

/// Set a single bit in a bitrow at bit_idx position.  Assume success, no bounds checking, so be careful!
/// Maybe this can graduate to bitbuffer.c someday?
void bitrow_set_bit(bitrow_t bitrow, unsigned bit_idx, unsigned bit_val)
{
    if (bit_val == 0) {
        bitrow[bit_idx >> 3] &= ~(1 << (7 - (bit_idx & 7)));
    }
    else {
        bitrow[bit_idx >> 3] |= (1 << (7 - (bit_idx & 7)));
    }
}

/// This is a differential PWM decode and is essentially only looking at symbol
/// transitions, not the symbols themselves.  An inverted bitstring would yield the
/// same result.  Note that:
///
/// - Initial contiguous alike symbol(s) is not considered data, regardless of length.
///   Essentially, the
///
/// - Any group of alike contiguous symbols with a length of 3 or more is considered
///   a sync.  If this happens anywhere except at the end of bitrow, any data already
///   decoded is discarded, the length and position of the sync is noted, and data
///   decoding resumes.
///
/// - The one or two alike contiguous symbols immediately after a sync are not treated
///   as data, they are essentially there to signify the end of the sync.
///
/// Return value is length of data decoded into bitrow_buf after last sync. If bitrow
/// ends with a sync, that sync is ignored and returned data will be data before that
/// sync.
///
/// Ensure that bitrow_buf is at least as big as bitrow or data overrun might occur.
///
/// Note that sync_pos and sync_len will be modified if a sync is found. If returned
/// sync_pos is greater than start, it might mean there is data between start and
/// sync_pos.  If desired, call again with bit_len = sync_pos to find this data.
///
/// Maybe this can graduate to bitbuffer.c someday?
unsigned bitrow_dpwm_decode(bitrow_t const bitrow, unsigned bit_len, unsigned start,
        bitrow_t bitrow_buf, unsigned *sync_pos, unsigned *sync_len)
{
    unsigned bitrow_pos;
    int bitrow_buf_pos          = -1;
    unsigned cur_symbol_len     = -1;
    *sync_pos                   = start;
    *sync_len                   = 0;
    unsigned sync_in_progress   = 1;
    unsigned prev_bit           = 0xff;  // So it's always different than the first bit
    unsigned this_bit;

    for (bitrow_pos = start; bitrow_pos < bit_len; bitrow_pos++) {
        this_bit = bitrow_get_bit(bitrow, bitrow_pos);
        if (this_bit == prev_bit) {
            if (++cur_symbol_len > 1) {
                sync_in_progress = 1;
            }
        }
        else {
            if (sync_in_progress) {
                *sync_len = cur_symbol_len + 1;
                *sync_pos = bitrow_pos - cur_symbol_len - 1;
                bitrow_buf_pos = -1;
                sync_in_progress = 0;
            }
            else {
                if (bitrow_buf_pos >= 0) {
                    bitrow_set_bit(bitrow_buf, bitrow_buf_pos, cur_symbol_len);
                }
                bitrow_buf_pos++;
            }
            cur_symbol_len = 0;
        }
        prev_bit = this_bit;
    }

    // If a sync was started at the end of the row, ignore it and the previous decoded bit
    if (sync_in_progress) {
        bitrow_buf_pos -= 1;
    }

    // If bad decode, just send back an empty result string.
    if (bitrow_buf_pos < 0) {
        bitrow_buf_pos = 0;
    }

    return bitrow_buf_pos;
}

static int directv_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int r;                   // a row index
    bitrow_t bitrow;         // space for a possibly modified bitbuffer row
    uint8_t bit_len;         // row length is variable, so need to keep track of this
    bitrow_t dtv_buf= {0};   // A location for our decoded bitrow data
    unsigned dtv_bit_len;
    unsigned row_sync_pos;
    unsigned row_sync_len;

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

    if ((bit_len < ROW_BITLEN_MIN) || (bit_len > ROW_BITLEN_MAX)) {
        if (decoder->verbose >= 2) {
            fprintf(stderr, "directv: incorrect number of bits in bitbuffer: %d (expected between %d and %d).\n", bit_len, ROW_BITLEN_MIN, ROW_BITLEN_MAX);
        }
        return 0;
    }

    bitbuffer_extract_bytes(bitbuffer, r, 0, bitrow, bit_len);

    // Decode the message symbols
    dtv_bit_len = bitrow_dpwm_decode(bitrow, bit_len, 0, dtv_buf, &row_sync_pos, &row_sync_len);
    if (decoder->verbose >= 2) {
        bitrow_printf(dtv_buf, dtv_bit_len, "directv: SYNC at pos:%u for %u symbols. DPWM Decoded Message: ", row_sync_pos, row_sync_len);
    }

    // Make sure we have exactly 40 bits (DTV_BITLEN_MAX)
    if (dtv_bit_len != DTV_BITLEN_MAX) {
        if (decoder->verbose >= 2) {
            fprintf(stderr, "directv: Incorrect number of decoded bits: %u (should be %d).\n", dtv_bit_len, DTV_BITLEN_MAX);
        }
        return 0;
    }

    // First byte should be 0x10 (model number?)
    if (dtv_buf[0] != 0x10) {
        if (decoder->verbose >= 2) {
            fprintf(stderr, "directv: Incorrect Model ID number: 0x%02X (should be 0x10).\n", dtv_buf[0]);
        }
        return 0;
    }

    // Validate Checksum
    unsigned checksum_1;
    unsigned checksum_2;
    checksum_1 = ( (dtv_buf[0] >> 4) + (dtv_buf[0] & 0x0F) + (dtv_buf[1] >> 4) + (dtv_buf[1] & 0x0F) +
                   (dtv_buf[2] >> 4) + (dtv_buf[2] & 0x0F) + (dtv_buf[3] >> 4) + (dtv_buf[3] & 0x0F) +
                   (dtv_buf[4] >> 4) ) & 0x0F;
    checksum_2 = dtv_buf[4] & 0x0F;
    if (checksum_1 != checksum_2) {
        if (decoder->verbose >= 2) {
            fprintf(stderr, "directv: Checksum failed: 0x%01X should match 0x%01X\n", checksum_1, checksum_2);
        }
        return 0;
    }

    // Get Device ID
    unsigned dtv_device_id;
    dtv_device_id = dtv_buf[1] << 12 | dtv_buf[2] << 4 | dtv_buf[3] >> 4;
    if (dtv_device_id > 999999) {
        if (decoder->verbose >= 2) {
            fprintf(stderr, "directv: Bad Device ID: %u (should be between 000000 and 999999).\n", dtv_device_id);
        }
        return 0;
    }

    // Get Button ID, assuming all byte values are valid.
    uint8_t dtv_button_id;
    dtv_button_id = dtv_buf[3] << 4 | dtv_buf[4] >> 4;

    // Populate our return fields
    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, "DirecTV-RC66RX",
            "id",            "",            DATA_FORMAT, "%06d", DATA_INT, dtv_device_id,
            "button_id",     "",            DATA_FORMAT, "0x%02X", DATA_INT, dtv_button_id,
            "button_name",   "",            DATA_FORMAT, "[%s]", DATA_STRING, get_dtv_button_label(dtv_button_id),
            "event",         "",            DATA_STRING, row_sync_len > ROW_SYNC_SHORT_LEN ? "INITIAL" : "REPEAT",
            "mic",           "",            DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "button_id",
        "button_name",
        "event",
        "mic",
        NULL,
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
