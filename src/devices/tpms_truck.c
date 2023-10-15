/**  @file
    Unbranded SolarTPMS for trucks.

    Copyright (C) 2021 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Unbranded SolarTPMS for trucks, with wheel counter, set of 6.

S.a. #1893

The preamble is 232 bit 0x55..5556.
The data packet is Manchester coded.

Specification:
- Monitoring temperature range: -40 C to 130 C
- Monitoring air pressure range: 0.1 bar to 12.0 bar
- 433 MHz FSK

Data layout (nibbles):

    U II II II II WW F PPP TT CC ?

- U: 4 bit state, decoding unknown, not included in checksum, could be sync
- I: 32 bit ID
- W: 8 bit wheel position
- F: 4 bit unknown flags (seen: 0x3)
- P: 12 bit Pressure (kPa)
- T: 8 bit Temperature (deg. C, possibly signed?)
- C: 8 bit Checksum (XOR on bytes 0 to 7)
- ?: 4 bit unknown (seems static)

Example data:

    ID:32h POS:8h FLAGS?4h KPA:12d TEMP:8d CHK:8h TRAILER?8h

    {401} 555555555555555555555555555555555555555555555555555555 5556 99 55 66 95 56 9a 59 95 56 55 59 5a 55 55 55 56 a9 96 9a aa  ff 80
    {401} 555555555555555555555555555555555555555555555555555555 5556 99 a5 66 96 65 99 a9 56 65 55 66 5a 55 55 55 56 a5 a5 59 aa  ff 80

    00000000000000000000000000001 a 0581b281 02 3 000 1e 9b f
    00000000000000000000000000001 a c594ae14 05 3 000 1c c2 f

*/

#include "decoder.h"

static int tpms_truck_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 76);

    if (packet_bits.bits_per_row[row] < 76) {
        return 0; // DECODE_FAIL_SANITY;
    }

    uint8_t b[9] = {0};
    bitbuffer_extract_bytes(&packet_bits, 0, 4, b, 72);

    int chk = xor_bytes(b, 9);
    if (chk != 0) {
        return 0; // DECODE_FAIL_MIC;
    }

    int state       = packet_bits.bb[0][0] >> 4; // fixed 0xa? could be sync
    unsigned id     = (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    int wheel       = b[4];
    int flags       = b[5] >> 4;
    int pressure    = (b[5] & 0x0f) << 8 | b[6];
    int temperature = b[7];

    char id_str[4 * 2 + 1];
    snprintf(id_str, sizeof(id_str), "%08x", id);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Truck",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "wheel",            "",             DATA_INT,    wheel,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.0f kPa",    DATA_DOUBLE, (float)pressure,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C",      DATA_DOUBLE, (float)temperature,
            "state",            "State?",       DATA_FORMAT, "%x",          DATA_INT,    state,
            "flags",            "Flags?",       DATA_FORMAT, "%x",          DATA_INT,    flags,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_truck_decode() */
static int tpms_truck_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // preamble
    uint8_t const preamble_pattern[3] = {0xaa, 0xaa, 0xa9}; // after invert

    unsigned bitpos = 0;
    int events      = 0;

    bitbuffer_invert(bitbuffer);
    // Find a preamble with enough bits after it that it could be a complete packet
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos, preamble_pattern, 24)) + 160 <=
            bitbuffer->bits_per_row[0]) {
        events += tpms_truck_decode(decoder, bitbuffer, 0, bitpos + 24);
        bitpos += 2;
    }

    return events;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "wheel",
        "pressure_kPa",
        "temperature_C",
        "state",
        "flags",
        "mic",
        NULL,
};

r_device const tpms_truck = {
        .name        = "Unbranded SolarTPMS for trucks",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,
        .long_width  = 52,
        .reset_limit = 150,
        .decode_fn   = &tpms_truck_callback,
        .fields      = output_fields,
};
