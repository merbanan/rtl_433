/** @file
    Microchip HCS361 KeeLoq Code Hopping Encoder based remotes.

    Copyright (C) 2023 Ethan Halsall

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/** @fn hcs361_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Microchip HCS361 KeeLoq Code Hopping Encoder based remotes.

Data Format:

66 bits transmitted, LSB first.

Extended Serial Number Disabled:

|  0-31 | Encrypted Portion
| 32-59 | Serial Number
| 60-63 | Button Status (S3, S0, S1, S2)
|  64   | Battery Low
| 65-66 | CRC

Extended Serial Number Enabled:

|  0-31 | Encrypted Portion
| 32-63 | Serial Number
|  64   | Battery Low
| 65-66 | CRC

Note that the button bits are (MSB/first sent to LSB) S3, S0, S1, S2.
Hardware buttons might map to combinations of these bits.

- Datasheet HCS361: https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/DataSheets/40146F.pdf

Known Devices:
- Manufacturer
  - Leer
- Model
  - OUTE_ELC (FCC ID KOBLEAR1XT)

Pulse Format / Timing:

PWM timings and code format varies based on EEPROM configuration.

Logic:
- 0 = long
- 1 = short

Timing is selected by the two flags coded into the EEPROM.

- TXWAK: Bit Format Select Or Wake-Up
  - When VPWM is enabled, this bit will enable the wake-up signal.
- BSEL: Baud Rate Select
  - When disabled, baud rate is 833 bits / second.
  - When enabled, baud rate is 1667 bits / second.

*/

#include "decoder.h"

static int hcs361_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    if (bitbuffer->num_rows != 2 || bitbuffer->bits_per_row[1] != 67)
        return DECODE_ABORT_LENGTH;

    if (bitbuffer->bits_per_row[0] == 6) {
        // sync
        if (bitbuffer->bb[0][0] != 0xfc) {
            return DECODE_ABORT_EARLY;
        }
    }
    else if (bitbuffer->bits_per_row[0] == 12) {
        // no sync
        if (bitbuffer->bb[0][0] != 0xff || bitbuffer->bb[0][1] != 0xf0) {
            return DECODE_ABORT_EARLY;
        }
    }
    else {
        return DECODE_ABORT_LENGTH;
    }

    // Second row is data
    uint8_t *b = bitbuffer->bb[1];

    // No need to decode/extract values for simple test
    if (b[1] == 0xff && b[2] == 0xff && b[3] == 0xff && b[4] == 0xff && b[5] == 0xff && b[6] == 0xff && b[7] == 0xff) {
        decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0xff");
        return DECODE_FAIL_SANITY;
    }

    int crc         = 0;
    int crc_bat_low = 0;
    int actual_crc  = (b[8] >> 5) & 0x3;

    for (int i = 0; i < 65; i++) {
        int bit     = (b[i / 8] >> (7 - (i % 8)));
        int crc_bit = ((crc >> 1) ^ bit) & 0x1;

        // datasheet recommends checking the final crc bit for bat low flag if it is flipped
        if (i == 64) {
            int crc_bit_bat_low = ((crc >> 1) ^ ~bit) & 0x1;
            crc_bat_low         = crc_bit_bat_low | (((crc_bit_bat_low ^ crc) << 1) & 0x2);
        }

        crc = crc_bit | (((crc_bit ^ crc) << 1) & 0x2);
    }

    if (actual_crc != crc && actual_crc != crc_bat_low) {
        return DECODE_FAIL_MIC;
    }

    // The transmission is LSB first, big endian.
    uint32_t encrypted = ((unsigned)reverse8(b[3]) << 24) | (reverse8(b[2]) << 16) | (reverse8(b[1]) << 8) | (reverse8(b[0]));
    int serial         = (reverse8(b[7] & 0xf0) << 24) | (reverse8(b[6]) << 16) | (reverse8(b[5]) << 8) | (reverse8(b[4]));
    int btn            = (b[7] & 0x0f);
    int btn_num        = (btn & 0x08) | ((btn & 0x01) << 2) | (btn & 0x02) | ((btn & 0x04) >> 2); // S3, S0, S1, S2
    int battery_ok     = (b[8] & 0x80) == 0x80;

    char encrypted_str[9];
    snprintf(encrypted_str, sizeof(encrypted_str), "%08X", encrypted);
    char serial_str[9];
    snprintf(serial_str, sizeof(serial_str), "%07X", serial);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING,    "Microchip-HCS361",
            "id",               "",             DATA_STRING,    serial_str,
            "battery_ok",       "Battery",      DATA_INT,       battery_ok,
            "button",           "Button",       DATA_INT,       btn_num,
            "encrypted",        "",             DATA_STRING,    encrypted_str,
            "mic",              "Integrity",    DATA_STRING,    "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "battery_ok",
        "button",
        "learn",
        "repeat",
        "encrypted",
        "mic",
        NULL,
};

/*
PWM Mode:
TXWAK: 0
BSEL:  0
TE:    (min: 260us), (avg: 400us), (max: 620us)
*/
r_device const hcs361_txwak_0_bsel_0 = {
        .name        = "Microchip HCS361 KeeLoq Hopping Encoder based remotes (315.1M Sync, 833 bit/s)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 400,            // Short     1x  TE
        .long_width  = 800,            // Long      2x  TE
        .gap_limit   = 1200,           // Gap       3x  TE
        .reset_limit = 7200,           // Reset     18x TE
        .tolerance   = 140,            // Tolerance 140 us
        .sync_width  = 4000,           // Sync      10x TE
        .decode_fn   = &hcs361_decode, // 111111     [sync 10x TE] [header 10x TE] [data] [guard time]
        .priority    = 1,              // prevent duplicate messages
        .fields      = output_fields,
};

/*
PWM Mode:
TXWAK: 0
BSEL:  1
TE:    (min: 130us), (avg: 200us), (max: 310us)
*/
r_device const hcs361_txwak_0_bsel_1 = {
        .name        = "Microchip HCS361 KeeLoq Hopping Encoder based remotes (315.1M Sync, 1667 bit/s)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200,            // Short     1x  TE
        .long_width  = 400,            // Long      2x  TE
        .gap_limit   = 600,            // Gap       3x  TE
        .reset_limit = 13600,          // Reset     34x TE
        .tolerance   = 70,             // Tolerance 70  us
        .sync_width  = 2000,           // Sync      10x TE
        .decode_fn   = &hcs361_decode, // 111111     [sync 10x TE] [header 10x TE] [data] [guard time]
        .priority    = 1,              // prevent duplicate messages
        .fields      = output_fields,
};

/*
PWM Mode:
TXWAK: 1
BSEL:  0
TE:    (min: 130us), (avg: 200us), (max: 310us)
*/
r_device const hcs361_txwak_1_bsel_0 = {
        .name        = "Microchip HCS361 KeeLoq Hopping Encoder based remotes (315.1M No Sync, 833 bit/s)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200,            // Short     1x  TE
        .long_width  = 400,            // Long      2x  TE
        .gap_limit   = 1200,           // Gap       6x  TE
        .reset_limit = 6800,           // Reset     34x TE
        .tolerance   = 140,            // Tolerance 70  us
        .decode_fn   = &hcs361_decode, // 1111111111               [header 10x TE] [data] [guard time]
        .priority    = 1,              // prevent duplicate messages
        .fields      = output_fields,
};

/*
PWM Mode:
TXWAK: 1
BSEL:  1
TE:    (min: 65us),  (avg: 100us), (max: 155us)
*/
r_device const hcs361_txwak_1_bsel_1 = {
        .name        = "Microchip HCS361 KeeLoq Hopping Encoder based remotes (315.1M No Sync, 1667 bit/s)",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 100,            // Short     1x  TE
        .long_width  = 200,            // Long      2x  TE
        .gap_limit   = 600,            // Gap       6x  TE
        .reset_limit = 6600,           // Reset     66x TE
        .tolerance   = 70,             // Tolerance 35  us
        .decode_fn   = &hcs361_decode, // 1111111111               [header 10x TE] [data] [guard time]
        .priority    = 1,              // prevent duplicate messages
        .fields      = output_fields,
};

/*
(Currently not implemented here)
VPWM Mode:
  BSEL=0
  - TE: (min: 260us), (avg: 400us), (max: 620us)
  - Guard Time: 114x TE

  BSEL=1
  - TE: (min: 130us), (avg: 200us), (max: 310us)
  - Guard Time: 226x TE
*/