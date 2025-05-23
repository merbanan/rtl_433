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

- Current Temperature of 4 probes, Meat (temp max target) and BBQ (temp range target LO and HI) with alarms
- Max Target temperatures (Meat Mode) or Low / High range Temperatures (BBQ Mode) are set on the transmitter but values are not transmited, only alarm flags if reached.

- Issue #3269

Flex decoder:

    rtl_433 -X "n=tp827b,m=FSK_PCM,s=110,l=110,r=2500,preamble=d2eceaee" *.cu8 2>&1 | grep codes

Data layout:

    Byte Position     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28
    Sample           d8 09 03 fe 00 fe 00 fe 00 fe 00 fe 00 fe 00 fe 00 31 1b 00 00 93 00 aa aa aa 00 00 00
    Data             II 11 11 FF FF 22 22 FF FF 33 33 FF FF 44 44 FF FF MX LT 0A 0H CC 00 aa aa aa 00 00 00
                                                                        || ||  |  |
                                        bit 0  0  1  1  Mode BBQ/Meat <-+| ||  |  +-> Alarm High bit 0  0  0  0
                                           M4 M3 M2 M1                   / ||  \                    H4 H3 H2 H1
                                        bit 0  0  0  1  Alarm <---------+  ||   +---> Alarm triggered
                                           A4 A3 A2 A1                    /  \
                                        bit 0  0  0  1 Alarm Low <-------+    +-----> Unit  1  0  1  1
                                           L4 L3 L2 L1                                     ?1 ?2 ?3 TU

- II :{8}  Model or ID
- 11 :{16} Temp Probe 1, C or F, scale 10, 0xFE00 = no probe
- FF :{16} Fixed value, 0xFE00
- 22 :{16} Temp Probe 2, C or F, scale 10, 0xFE00 = no probe
- FF :{16} Fixed value, 0xFE00
- 33 :{16} Temp Probe 3, C or F, scale 10, 0xFE00 = no probe
- FF :{16} Fixed value, 0xFE00
- 44 :{16} Temp Probe 4, C or F, scale 10, 0xFE00 = no probe
- FF :{16} Fixed value, 0xFE00
- M  :{4}  Mode, 0 = BBQ/Low/High range, 1 = Meat target Temp
- X  :{4}  Alarm flags by probe
- L  :{4}  Low Temp Alarm flags: L1 = Probe 1 Low Temp reached, L2 = Probe 2 Low Temp reached ...
- T  :{4}  First 3 bits unknown, last bit TU, Temperature unit flag, 1 = Fahrenheit, 0 = Celsius
- 0  :     Fixed 0
- A  :{2}  Alarm ON, 0x2 or 0x3
- H  :{4}  High Temp Alarm flags: H1 = Probe 1 High Temp reached, H2 = Probe 2 High Temp reached ...
- CC :{8}  CRC-8/SMBUS, poly 0x07, init 0x00, final XOR 0x00 from 21 previous bytes.
- 00 :{8}  Fixed 0x00
- aa :{24} Fixed 0xaa values
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

    if ( b[3] !=0xFE || b[4] != 0x00 || b[7] !=0xFE || b[8] != 0x00 || b[11] !=0xFE || b[12] != 0x00 || b[15] !=0xFE || b[16] != 0x00) {
        decoder_log(decoder, 1, __func__, "Fixed values mismatch");
        return DECODE_FAIL_SANITY;
    }

    if (crc8(b, 22, 0x07, 0x00)) {
        decoder_logf(decoder, 1, __func__, "CRC Error, expected: %02x", crc8(b, 21, 0x07, 0x00));
        return DECODE_FAIL_MIC;
    }

    decoder_log_bitrow(decoder, 2, __func__, b, 176, "MSG");

    int id     = b[0];
    int p1_raw = (int16_t)((b[1] << 8) | b[2]);
    int p2_raw = (int16_t)((b[5] << 8) | b[6]);
    int p3_raw = (int16_t)((b[9] << 8) | b[10]);
    int p4_raw = (int16_t)((b[13] << 8) | b[14]);

    float p1_temp = p1_raw * 0.1f;
    float p2_temp = p2_raw * 0.1f;
    float p3_temp = p3_raw * 0.1f;
    float p4_temp = p4_raw * 0.1f;

    int mode_raw   = (b[17] & 0xF0) >> 4;
    int mode_p1    =  mode_raw & 0x1;
    int mode_p2    = (mode_raw & 0x2) >> 1;
    int mode_p3    = (mode_raw & 0x4) >> 2;
    int mode_p4    = (mode_raw & 0x8) >> 3;

    int alarm_raw  = (b[17] & 0x0F);
    int alarm_p1   =  alarm_raw & 0x1;
    int alarm_p2   = (alarm_raw & 0x2) >> 1;
    int alarm_p3   = (alarm_raw & 0x4) >> 2;
    int alarm_p4   = (alarm_raw & 0x8) >> 3;

    int temp_unit  = b[18] & 0x01;

    int flags_low  = (b[18] & 0xF0) >> 4;
    int low_p1     =  flags_low & 0x1;
    int low_p2     = (flags_low & 0x2) >> 1;
    int low_p3     = (flags_low & 0x4) >> 2;
    int low_p4     = (flags_low & 0x8) >> 3;

    int flags_high = b[20] & 0x0F;
    int high_p1    =  flags_high & 0x1;
    int high_p2    = (flags_high & 0x2) >> 1;
    int high_p3    = (flags_high & 0x4) >> 2;
    int high_p4    = (flags_high & 0x8) >> 3;

    int flags1     = b[18] & 0x0F; // 3 first bits not guessed, last bit always 1 if Fahrenheit, 0 if Celsius
    int alarm_on   = b[19];

    /* clang-format off */
    data_t *data = data_make(
            "model",                "",              DATA_STRING,        "ThermoPro-TB827B",
            "id",                   "",              DATA_FORMAT,        "%02x",                          DATA_INT,    id,
            "temp_unit",            "Display Unit",  DATA_STRING, temp_unit ? "Fahrenheit" : "Celsius",
            "mode_p1",              "Mode 1",        DATA_STRING, mode_p1 ? "Meat" : "BBQ",
            "mode_p2",              "Mode 2",        DATA_STRING, mode_p2 ? "Meat" : "BBQ",
            "mode_p3",              "Mode 3",        DATA_STRING, mode_p3 ? "Meat" : "BBQ",
            "mode_p4",              "Mode 4",        DATA_STRING, mode_p4 ? "Meat" : "BBQ",
            "temperature_1_C",      "Temperature 1", DATA_COND,   temp_unit == 0x0 && p1_temp != -51.2f , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p1_temp, // if 0xfe00 then no probe
            "temperature_2_C",      "Temperature 2", DATA_COND,   temp_unit == 0x0 && p2_temp != -51.2f , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p2_temp, // if 0xfe00 then no probe
            "temperature_3_C",      "Temperature 3", DATA_COND,   temp_unit == 0x0 && p3_temp != -51.2f , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p3_temp, // if 0xfe00 then no probe
            "temperature_4_C",      "Temperature 4", DATA_COND,   temp_unit == 0x0 && p4_temp != -51.2f , DATA_FORMAT, "%.1f C", DATA_DOUBLE, p4_temp, // if 0xfe00 then no probe
            "temperature_1_F",      "Temperature 1", DATA_COND,   temp_unit == 0x1 && p1_temp != -51.2f , DATA_FORMAT, "%.1f F", DATA_DOUBLE, p1_temp, // if 0xfe00 then no probe
            "temperature_2_F",      "Temperature 2", DATA_COND,   temp_unit == 0x1 && p2_temp != -51.2f , DATA_FORMAT, "%.1f F", DATA_DOUBLE, p2_temp, // if 0xfe00 then no probe
            "temperature_3_F",      "Temperature 3", DATA_COND,   temp_unit == 0x1 && p3_temp != -51.2f , DATA_FORMAT, "%.1f F", DATA_DOUBLE, p3_temp, // if 0xfe00 then no probe
            "temperature_4_F",      "Temperature 4", DATA_COND,   temp_unit == 0x1 && p4_temp != -51.2f , DATA_FORMAT, "%.1f F", DATA_DOUBLE, p4_temp, // if 0xfe00 then no probe
            "alarm_low_1",          "Alarm Low 1",   DATA_COND, !mode_p1, DATA_INT, (alarm_p1 && low_p1 && !high_p1) ? 1 : 0,
            "alarm_high_1",         "Alarm High 1",  DATA_COND, !mode_p1, DATA_INT, (alarm_p1 && low_p1 &&  high_p1) ? 1 : 0,
            "alarm_meat_1",         "Alarm Meat 1",  DATA_COND,  mode_p1, DATA_INT, (alarm_p1 && low_p1)             ? 1 : 0,
            "alarm_low_2",          "Alarm Low 2",   DATA_COND, !mode_p2, DATA_INT, (alarm_p2 && low_p2 && !high_p2) ? 1 : 0,
            "alarm_high_2",         "Alarm High 2",  DATA_COND, !mode_p2, DATA_INT, (alarm_p2 && low_p2 &&  high_p2) ? 1 : 0,
            "alarm_meat_2",         "Alarm Meat 2",  DATA_COND,  mode_p2, DATA_INT, (alarm_p2 && low_p2) ? 1 : 0,
            "alarm_low_3",          "Alarm Low 3",   DATA_COND, !mode_p3, DATA_INT, (alarm_p3 && low_p3 && !high_p3) ? 1 : 0,
            "alarm_high_3",         "Alarm High 3",  DATA_COND, !mode_p3, DATA_INT, (alarm_p3 && low_p3 &&  high_p3) ? 1 : 0,
            "alarm_meat_3",         "Alarm Meat 3",  DATA_COND,  mode_p3, DATA_INT, (alarm_p3 && low_p3) ? 1 : 0,
            "alarm_low_4",          "Alarm Low 4",   DATA_COND, !mode_p4, DATA_INT, (alarm_p4 && low_p4 && !high_p4) ? 1 : 0,
            "alarm_high_4",         "Alarm High 4",  DATA_COND, !mode_p4, DATA_INT, (alarm_p4 && low_p4 &&  high_p4) ? 1 : 0,
            "alarm_meat_4",         "Alarm Meat 4",  DATA_COND,  mode_p4, DATA_INT, (alarm_p4 && low_p4) ? 1 : 0,
            "alarm_on",             "Alarm ON",      DATA_INT,  (alarm_on > 0) ? 1 : 0,
            "flags1",               "Flags",         DATA_FORMAT,  "%04b",  DATA_INT, flags1,
            "mic",                  "Integrity",     DATA_STRING,  "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const tb827b_output_fields[] = {
        "model",
        "id",
        "temp_unit",
        "mode_p1",
        "mode_p2",
        "mode_p3",
        "mode_p4",
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
        "alarm_meat_1",
        "alarm_meat_2",
        "alarm_meat_3",
        "alarm_meat_4",
        "alarm_on",
        "flags0",
        "flags1",
        "flags2",
        "flags3",
        "mic",
        NULL,
};

r_device const thermopro_tb827b = {
        .name        = "ThermoPro TP827B BBQ Meat Thermometers 4 probes with Temp, Meat and BBQ Target LO and HI alarms",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 110,
        .long_width  = 110,
        .reset_limit = 2500,
        .tolerance   = 5,
        .decode_fn   = &thermopro_tb827b_decode,
        .fields      = tb827b_output_fields,
};
