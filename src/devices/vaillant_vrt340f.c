/** @file
    Vaillant VRT 340f (calorMatic 340f) central heating control.

    Copyright (C) 2017 Reinhold Kainhofer <reinhold@kainhofer.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
Vaillant VRT 340f (calorMatic 340f) central heating control.

    http://wiki.kainhofer.com/hardware/vaillantvrt340f

The data is sent differential Manchester encoded
with bit-stuffing (after five 1 bits an extra 0 bit is inserted)

All bytes are sent with least significant bit FIRST (1000 0111 = 0xE1)

    0x00 00 7E | 6D F6 | 00 20 00 | 00 | 80 | B4 | 00 | FD 49 | FF 00
      SYNC+HD. | DevID | CONST?   |Rep.|Wtr.|Htg.|Btr.|Checksm| EPILOGUE

- CONST? ... Unknown, but constant in all observed signals
- Rep.   ... Repeat indicator: 0x00=original signal, 0x01=first repeat
- Wtr.   ... pre-heated Water: 0x80=ON, 0x88=OFF (bit 8 is always set)
- Htg.   ... Heating: 0x00=OFF, 0xB4=ON (2-point), 0x01-0x7F=target heating water temp
             (bit 8 indicates 2-point heating mode, bits 1-7 the heating water temp)
- Btr.   ... Battery: 0x00=OK, 0x01=LOW
- Checksm... Checksum (2-byte signed int): = -sum(bytes 4-12)

*/

#include "decoder.h"

static int validate_checksum(r_device *decoder, uint8_t *b, int from, int to, int cs_from, int cs_to)
{
    // Fields cs_from and cs_to hold the 2-byte checksum as signed int
    int expected   = (b[cs_from] << 8) | b[cs_to];
    int calculated = add_bytes(&b[from], to - from + 1);
    int chk        = (calculated + expected) & 0xffff;

    if (chk) {
        decoder_logf(decoder, 1, __func__, "Checksum error in Vaillant VRT340f.  Expected: %04x  Calculated: %04x", expected, calculated);
        decoder_logf_bitrow(decoder, 1, __func__, &b[from], (to - from + 1) * 8, "Message (data content of bytes %d-%d)", from, to);
    }
    return !chk;
}

static int vaillant_vrt340_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t *b = bitbuffer->bb[0];

    // TODO: Use repeat signal for error checking / correction!

    // each row needs to have at least 128 bits (plus a few more due to bit stuffing)
    if (bitbuffer->bits_per_row[0] < 128)
        return DECODE_ABORT_LENGTH;

    // The protocol uses bit-stuffing => remove 0 bit after five consecutive 1 bits
    // Also, each byte is represented with least significant bit first -> swap them!
    bitbuffer_t bits = {0};
    int ones = 0;
    for (uint16_t k = 0; k < bitbuffer->bits_per_row[0]; k++) {
        int bit = bitrow_get_bit(b, k);
        if (bit == 1) {
            bitbuffer_add_bit(&bits, 1);
            ones++;
        } else {
            if (ones != 5) { // Ignore a 0 bit after five consecutive 1 bits:
                bitbuffer_add_bit(&bits, 0);
            }
            ones = 0;
        }
    }

    b = bits.bb[0];
    uint16_t bitcount = bits.bits_per_row[0];

    // Change to least-significant-bit last (protocol uses least-significant-bit first)
    reflect_bytes(b, (bitcount - 1) / 8);

    // A correct message has 128 bits plus potentially two extra bits for clock sync at the end
    if (!(128 <= bitcount && bitcount <= 131) && !(168 <= bitcount && bitcount <= 171))
        return DECODE_ABORT_LENGTH;

    // "Normal package":
    if ((b[0] == 0x00) && (b[1] == 0x00) && (b[2] == 0x7e) && (128 <= bitcount && bitcount <= 131)) {

        if (!validate_checksum(decoder, b, /* Data from-to: */3, 11, /*Checksum from-to:*/12, 13)) {
            return DECODE_FAIL_MIC;
        }

        // Device ID starts at byte 4:
        int device_id          = (b[3] << 8) | b[4];
        int heating_mode       = (b[10] >> 7);    // highest bit indicates automatic (2-point) / analogue mode
        int target_temperature = (b[10] & 0x7f);  // highest bit indicates auto(2-point) / analogue mode
        int water_preheated    = (b[9] & 8) == 0; // bit 4 indicates water: 1=Pre-heat, 0=no pre-heated water
        int battery_low        = b[11] != 0;      // if not zero, battery is low

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",                     DATA_STRING, "Vaillant-VRT340f",
                "id",           "Device ID",            DATA_FORMAT, "0x%04X", DATA_INT, device_id,
                "heating",      "Heating Mode",         DATA_STRING, (heating_mode == 0 && target_temperature == 0) ? "OFF" : heating_mode ? "ON (2-point)" : "ON (analogue)",
                "heating_temp", "Heating Water Temp.",  DATA_FORMAT, "%d", DATA_INT, target_temperature,
                "water",        "Pre-heated Water",     DATA_STRING, water_preheated ? "ON" : "off",
                "battery_ok",   "Battery",              DATA_INT,    !battery_low,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);

        return 1;
    }

    // "RF detection package":
    if ((b[0] == 0x00) && (b[1] == 0x00) && (b[2] == 0x7E) && (168 <= bitcount && bitcount <= 171)) {

        if (!validate_checksum(decoder, b, /* Data from-to: */ 3, 16, /*Checksum from-to:*/ 17, 18)) {
            return DECODE_FAIL_MIC;
        }

        // Device ID starts at byte 12:
        int device_id = (b[11] << 8) | b[12];

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",                     DATA_STRING, "Vaillant-VRT340f",
                "id",           "Device ID",            DATA_INT,    device_id,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);

        return 1;
    }

    return DECODE_FAIL_SANITY;
}

static char const *const output_fields[] = {
        "model",
        "id",
        "heating",
        "heating_temp",
        "water",
        "battery_ok",
        NULL,
};

r_device const vaillant_vrt340f = {
        .name        = "Vaillant calorMatic VRT340f Central Heating Control",
        .modulation  = OOK_PULSE_DMC,
        .short_width = 836,  // half-bit width 836 us
        .long_width  = 1648, // bit width 1648 us
        .reset_limit = 4000,
        .tolerance   = 120, // us
        .decode_fn   = &vaillant_vrt340_callback,
        .fields      = output_fields,
};
