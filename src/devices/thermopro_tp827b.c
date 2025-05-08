/** @file
    ThermoPro TP827B BBQ Meat Thermometer 4 probes.

    Copyright (C) 2025 Bruno OCTAU (\@ProfBoc75)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
ThermoPro TP827B BBQ Meat Thermometer 4 probes.

- Current Temperature of 4 probes, 3 modes, Normal (no temp target), Meat (temp max target) and BBQ (temp range target LO and HI) with alarms
- Max Target temperatures (Meat Mode) or Low / High range Temperatures (BBQ Mode) are set on the transmitter but values are not transmited, only alarm flags if reached.

- Issue #3269

Flex decoder:

    rtl_433 -X "n=tp827b,m=FSK_PCM,s=110,l=110,r=2500,preamble=d2eceaee" *.cu8 2>&1 | grep codes

Data layout:

    Byte Position     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28
    Sample           d8 09 03 fe 00 fe 00 fe 00 fe 00 fe 00 fe 00 fe 00 31 1b 00 00 93 00 aa aa aa 00 00 00
    Data             II 11 11 FF FF 22 22 FF FF 33 33 FF FF 44 44 FF FF MX LT 00 0H CC 00 AA AA AA 00 00 00
                                                                        || ||     |
                                                             Mode <-----+| ||     +-----> Alarm High bit 0  0  0  0
                                                          Unknown <------+ ||                           H4 H3 H2 H1
                                         bit 0  0  0  0 Alarm Low <--------++-----------> Unit  0  0  0  1
                                            L4 L3 L2 L1                                        ?1 ?2 ?3 TU

- II :{8}  Model or ID
- 11 :{16} Temp Probe 1, C or F, scale 10, 0xFE00 = no probe
- FF :{16} Fixed value, 0xFE00
- 22 :{16} Temp Probe 2, C or F, scale 10, 0xFE00 = no probe
- FF :{16} Fixed value, 0xFE00
- 33 :{16} Temp Probe 3, C or F, scale 10, 0xFE00 = no probe
- FF :{16} Fixed value, 0xFE00
- 44 :{16} Temp Probe 4, C or F, scale 10, 0xFE00 = no probe
- FF :{16} Fixed value, 0xFE00
- M  :{4}  Mode, Normal (no alarm set) = 3, BBQ = 0xC, Meat = 0xF
- X  :{4}  Unknown flags, from the first sample = 1, but from last codes always = 0
- L  :{4}  Low Temp Alarm flags: L1 = Probe 1 Low Temp reached, L2 = Probe 2 Low Temp reached ...
- T  :{4}  First 3 bits unknown, last bit TU, Temperature unit flag, 1 = Fahrenheit, 0 = Celsius
- 0  :{12} Fixed 0x000
- H  :{4}  High Temp Alarm flags: H1 = Probe 1 High Temp reached, H2 = Probe 2 High Temp reached ...
- CC :{8}  CRC-8/SMBUS, poly 0x07, init 0x00, final XOR 0x00 from 21 previous bytes.
- 00 :{8}  Fixed 0x00
- AA :{24} Fixed 0xAA values
- 00 :{n}  Trailed zeros

*/
static int thermopro_tb827b_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { // 0xd2, removed to increase success
                                        0xec, 0xea, 0xee};
    uint8_t b[22];

    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }
    int msg_len = bitbuffer->bits_per_row[0];

    if (msg_len > 340) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    int offset = bitbuffer_search(bitbuffer, 0, 0, preamble_pattern, sizeof(preamble_pattern) * 8);

    if (offset >= msg_len) {
        decoder_log(decoder, 1, __func__, "Sync word not found");
        return DECODE_ABORT_EARLY;
    }

    if ((msg_len - offset ) < 176 ) {
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    }

    offset += sizeof(preamble_pattern) * 8;
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 22 * 8);

    if (crc8(b, 22, 0x07, 0x00)) {
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, 176, "MSG");

    int id     = b[0];
    int p1_raw = (b[1] << 8) | b[2];
    int p2_raw = (b[5] << 8) | b[6];
    int p3_raw = (b[9] << 8) | b[10];
    int p4_raw = (b[13] << 8) | b[14];

    float p1_temp = p1_raw * 0.1f;
    float p2_temp = p2_raw * 0.1f;
    float p3_temp = p3_raw * 0.1f;
    float p4_temp = p4_raw * 0.1f;

    int mode       = (b[17] & 0xF0) >> 4;
    int display_u  = b[18] & 0x01;

    int flags_low  = (b[18] & 0xF0) >> 4;
    int low_p1     = flags_low & 0x1;
    int low_p2     = (flags_low & 0x2) >> 1;
    int low_p3     = (flags_low & 0x4) >> 2;
    int low_p4     = (flags_low & 0x8) >> 3;

    int flags_high = b[20] & 0x0F;
    int high_p1     = flags_high & 0x1;
    int high_p2     = (flags_high & 0x2) >> 1;
    int high_p3     = (flags_high & 0x4) >> 2;
    int high_p4     = (flags_high & 0x8) >> 3;

    int flags1      = b[17] & 0x0F; // always 0
    int flags2      = b[18] & 0x0F; // always 1 if Fahrenheit, 0 if Celsius

    /* clang-format off */
    data_t *data = data_make(
            "model",                "",              DATA_STRING,        "ThermoPro-TB827B",
            "id",                   "",              DATA_FORMAT,        "%02x",                       DATA_INT,    id,
            "display_u",            "Display Unit",  DATA_COND, display_u == 0x1,                      DATA_STRING, "Fahrenheit",
            "display_u",            "Display Unit",  DATA_COND, display_u == 0x0,                      DATA_STRING, "Celsius",
            "mode",                 "Mode",          DATA_COND, mode == 0xF,                           DATA_STRING, "Meat",
            "mode",                 "Mode",          DATA_COND, mode == 0xC,                           DATA_STRING, "BBQ",
            "mode",                 "Mode",          DATA_COND, mode == 0x3,                           DATA_STRING, "Normal",
            "temperature_1_C",      "Temperature 1", DATA_COND, p1_raw != 0xfe00 && display_u == 0x0 , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p1_temp, // if 0xfe00 then no probe
            "temperature_2_C",      "Temperature 2", DATA_COND, p2_raw != 0xfe00 && display_u == 0x0 , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p2_temp, // if 0xfe00 then no probe
            "temperature_3_C",      "Temperature 3", DATA_COND, p3_raw != 0xfe00 && display_u == 0x0 , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p3_temp, // if 0xfe00 then no probe
            "temperature_4_C",      "Temperature 4", DATA_COND, p4_raw != 0xfe00 && display_u == 0x0 , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p4_temp, // if 0xfe00 then no probe
            "temperature_1_F",      "Temperature 1", DATA_COND, p1_raw != 0xfe00 && display_u == 0x1 , DATA_FORMAT, "%.1f F", DATA_DOUBLE, p1_temp, // if 0xfe00 then no probe
            "temperature_2_F",      "Temperature 2", DATA_COND, p2_raw != 0xfe00 && display_u == 0x1 , DATA_FORMAT, "%.1f F", DATA_DOUBLE, p2_temp, // if 0xfe00 then no probe
            "temperature_3_F",      "Temperature 3", DATA_COND, p3_raw != 0xfe00 && display_u == 0x1 , DATA_FORMAT, "%.1f F", DATA_DOUBLE, p3_temp, // if 0xfe00 then no probe
            "temperature_4_F",      "Temperature 4", DATA_COND, p4_raw != 0xfe00 && display_u == 0x1 , DATA_FORMAT, "%.1f F", DATA_DOUBLE, p4_temp, // if 0xfe00 then no probe
            "alarm_low_1",          "Alarm Low 1",   DATA_COND, low_p1 && mode == 0xC, DATA_INT, 1,  // low temp reached in range mode BBQ
            "alarm_low_2",          "Alarm Low 2",   DATA_COND, low_p2 && mode == 0xC, DATA_INT, 1,  // low temp reached in range mode BBQ
            "alarm_low_3",          "Alarm Low 3",   DATA_COND, low_p3 && mode == 0xC, DATA_INT, 1,  // low temp reached in range mode BBQ
            "alarm_low_4",          "Alarm Low 4",   DATA_COND, low_p4 && mode == 0xC, DATA_INT, 1,  // low temp reached in range mode BBQ
            "alarm_high_1",         "Alarm High 1",  DATA_COND, high_p1 && mode == 0xC, DATA_INT, 1, // high temp reached in range mode BBQ
            "alarm_high_2",         "Alarm High 2",  DATA_COND, high_p2 && mode == 0xC, DATA_INT, 1, // high temp reached in range mode BBQ
            "alarm_high_3",         "Alarm High 3",  DATA_COND, high_p3 && mode == 0xC, DATA_INT, 1, // high temp reached in range mode BBQ
            "alarm_high_4",         "Alarm High 4",  DATA_COND, high_p4 && mode == 0xC, DATA_INT, 1, // high temp reached in range mode BBQ
            "alarm_max_1",          "Alarm Max 1",   DATA_COND, low_p1 && mode == 0xF, DATA_INT, 1, // max temp reached in mode Meat
            "alarm_max_2",          "Alarm Max 2",   DATA_COND, low_p2 && mode == 0xF, DATA_INT, 1, // max temp reached in mode Meat
            "alarm_max_3",          "Alarm Max 3",   DATA_COND, low_p3 && mode == 0xF, DATA_INT, 1, // max temp reached in mode Meat
            "alarm_max_4",          "Alarm Max 4",   DATA_COND, low_p4 && mode == 0xF, DATA_INT, 1, // max temp reached in mode Meat
            "flags1",               "Flags1",        DATA_FORMAT,    "%1x",         DATA_INT, flags1,
            "flags2",               "Flags2",        DATA_FORMAT,    "%1x",         DATA_INT, flags2,
            "mic",                  "Integrity",     DATA_STRING,    "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const tb827b_output_fields[] = {
        "model",
        "id",
        "display_u",
        "mode",
        "temperature_1_C",
        "temperature_2_C",
        "temperature_3_C",
        "temperature_4_C",
        "temperature_1_F",
        "temperature_2_F",
        "temperature_3_F",
        "temperature_4_F",
        "alarm_low_1",
        "alarm_low_2",
        "alarm_low_3",
        "alarm_low_4",
        "alarm_high_1",
        "alarm_high_2",
        "alarm_high_3",
        "alarm_high_4",
        "alarm_max_1",
        "alarm_max_2",
        "alarm_max_3",
        "alarm_max_4",
        "flags1",
        "flags2",
        "mic",
        NULL,
};

r_device const thermopro_tb827b = {
        .name        = "ThermoPro TP827B BBQ Meat Thermometers 4 probes with Temp, Meat and BBQ Target LO and HI alarms",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 110,
        .long_width  = 110,
        .reset_limit = 2500,
        .decode_fn   = &thermopro_tb827b_decode,
        .fields      = tb827b_output_fields,
};
