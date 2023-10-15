/** @file
    Hyundai TPMS (VDO) FSK 10 byte Manchester encoded CRC-8 TPMS data.

    Copyright (C) 2020 Todor Uzunov aka teou, TTiges, 2019 Andreas Spiess, 2017 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Hyundai TPMS (VDO) FSK 10 byte Manchester encoded CRC-8 TPMS data.

Tested on a Hyundai i30 PDE. It uses sensors from Continental/VDO. VDO reference/part no.: A2C98607702, generation TG1C, FCC ID: KR5TIS-01
Similar sensors and probably protocol are used in models from BMW, Fiat-Chrysler-Alfa, Peugeot-Citroen, Hyundai-KIA, Mitsubishi, Mazda, etc.
https://www.vdo.com/media/746526/2019-10_tpms-oe-sensors_application-list.pdf
Hence my compilation and fine-tuning the already present as of late 2020 source for Citroen, Abarth and Jansite.

- Working Temperature: -50°C to 125°C (but according to some sources the chip can only handle -40°C)
- Working Frequency: 433.92MHz+-38KHz
- Tire monitoring range value: 0kPa-350kPa+-7kPa

Packet nibbles:

    PRE    UU  IIIIIIII FR  PP TT BB  CC

- PRE = preamble is 55 55 55 56 (inverted: aa aa aa a9)
- U = state, decoding unknown. In all tests has values 20,21,22,23 in hex.
    Probably codes the information how was the sensor activated - external service tool or internal accelometer (first byte) and the speed of transmission?
- I = sensor Id in hex
- F = Flags, in my tests always 0. Most probably here are coded in separate bits the alert flags for low pressure (below 150kPa), temperature and low battery.
    So it should be something like 00 (0) - all ok, 01 (1) - low pressure, 11 (3) - low pressure and low battery and so on, but more testing is necessary
- R = packet Repetition in every burst (about 10-11 identical packets are transmitted in every burst approximately once per 64 seconds)
- P = Pressure X/5=PSI or X(dec).1.375=kPa
- T = Temperature (deg C offset by 50)
- B = Battery and/or acceleration? Since decoding is currently unknown i will just leave it as a value
- C = CRC-8 with poly 0x07 init 0xaa, including the first byte (UU), unlike in some other similar protocols
*/

#include "decoder.h"

static int tpms_hyundai_vdo_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    int state;
    unsigned id;
    int flags;
    int repeat;
    int pressure;
    int temperature;
    int maybe_battery;
    int crc;

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 80);

    if (packet_bits.bits_per_row[0] < 80) {
        return DECODE_FAIL_SANITY; // too short to be a whole packet
    }

    b = packet_bits.bb[0];

//  if (b[6] == 0 || b[7] == 0) {
//      return DECODE_ABORT_EARLY; // pressure cannot really be 0, temperature is also probably not -50C
//  }

    crc = b[9];
    if (crc8(b, 9, 0x07, 0xaa) != crc) {
        return 0;
    }

    state         = b[0];
    id            = (unsigned)b[1] << 24 | b[2] << 16 | b[3] << 8 | b[4];
    flags         = b[5] >> 4;
    repeat        = b[5] & 0x0f;
    pressure      = b[6];
    temperature   = b[7];
    maybe_battery = b[8];

    char id_str[9 + 1];
    snprintf(id_str, sizeof(id_str), "%08x", id);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Hyundai-VDO",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "state",            "",             DATA_INT,    state,
            "flags",            "",             DATA_INT,    flags,
            "repeat",           "repetition",   DATA_INT,    repeat,
            "pressure_kPa",     "pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure * 1.375,
            "temperature_C",    "temp",         DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature - 50.0,
            "maybe_battery",    "",             DATA_INT,    maybe_battery,
            "mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Wrapper for the Hyundai-VDO tpms.
@sa tpms_hyundai_vdo_decode()
*/
static int tpms_hyundai_vdo_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // full preamble is 55 55 55 56 (inverted: aa aa aa a9)
    uint8_t const preamble_pattern[4] = {0xaa, 0xaa, 0xaa, 0xa9};

    unsigned bitpos = 0;
    int ret         = 0;
    int events      = 0;

    bitbuffer_invert(bitbuffer);

    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 32)) + 80 <=
            bitbuffer->bits_per_row[0]) {
        ret = tpms_hyundai_vdo_decode(decoder, bitbuffer, 0, bitpos + 32);
        if (ret > 0)
            events += ret;
        bitpos += 2;
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "state",
        "flags",
        "repeat",
        "pressure_kPa",
        "temperature_C",
        "maybe_battery",
        "mic",
        NULL,
};

r_device const tpms_hyundai_vdo = {
        .name        = "Hyundai TPMS (VDO)",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // in the FCC test protocol is actually 42us, but works with 52 also
        .long_width  = 52,  // FSK
        .reset_limit = 150, // Maximum gap size before End Of Message [us].
        .decode_fn   = &tpms_hyundai_vdo_callback,
        .fields      = output_fields,
};
