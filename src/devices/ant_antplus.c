/** @file
    ANT and ANT+ decoder.

    Copyright (C) 2022 Roberto Cazzaro <https://github.com/robcazzaro>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
ANT and ANT+ decoder.

ANT and ANT+ comunication standards are defined by a division of Garmin
https://www.thisisant.com/ and used widely for low power devices.
The ANT radio transmits for less than 150 µs per message, allowing a single
channel to be divided into hundreds of time slots and avoiding collisions.
ANT and ANT+ devices use a modified Shockburst protocol, in the 2.4GHz ISM band,
with 160kHz deviation and 1Mbps data rates, GFSK encoded. The low level
layer is not documented anywhere. ANT chips use an 8 byte key to generate a 2 byte
network ID using an unspecified algorithm. Valid keys are only assigned by Garmin and
require specific licensing terms (and in some cases a payment)
ANT+ uses the basic ANT message structure but is a managed network with a specific
network key and well defined device types, each sending "data pages" of 8 bytes
with specific data for the device type. Most ANT+ devices are sports focused like
heart rate monitors, bicycle sensors, or environmental sensors.

Please note that unlike most devices in the rtl_433 repository, ANT+ devices
operate in the ISM band between 2.4GHz and 2.5GHz. Decoding these signals
requires an SDR capable of operating above 2.4GHz (e.g. PlutoSDR) or the
use of a downconverter for rtl_sdr. Finally, since the protocol encodes at
1Mbps, sampling rate should be -s 4M or higher. It can work with a sampling
rate as low as 2Msps, but unreliably.

Without knowing the 8 byte key, existing ANT chips do not allow sniffing
of the data packets. Hence the need for this decoder. At the moment it's
not possible to recover the ANT key from the 16 bit on-air network key.

This decoder only captures and displays the low level packets, identifying the
2 byte network key plus all other device characteristics and 8 byte ANT payload.
It identifies ANT+ packets using the unique network key used (0xa6c5)
Refer to ANT+ documentation for ech specific device to parse the content of
the pages sent as 8 byte ANT+ payload.

The ANT protocol is using an uncommon strategy for the premable: either 0x55 or 0xaa,
depending on the value of the first bit of the following byte (0 and 1 respectively)
nRF24L01, on which the ANT protocol is based, uses the same premable strategy.
In order to determine if the packet is using 0x55 or 0xaa, both packets are extracted
and the only valid one is determined by matching the packet CRC with a calculated value

The unpacked payload is 18 bytes long structured as follows:

    PNNDDTXLPPPPPPPPCC

- P: Preamble: either 0x55 or 0xAA, depending on the value of first bit of the next byte
- N: Network key, assume LSB first (ANT+ uses 0xc5a6, most invalid keys 0x255b)
- D: Device number, 16 bit. LSB first
- X: Transmission type
- L: ANT payload length including CRC
- P: 8 byte ANT or ANT+ payload
- C: 16 bit CRC (CRC-16/CCITT-FALSE)

L is always 10, because at the moment the ANT payload is always 8 bytes

CREDITS:
https://github.com/sghctoma/antfs-poc-defcon24
https://reveng.sourceforge.io/ to reverse engineer the CRC algorithm used
*/

#include "decoder.h"

static uint16_t crc16ccitt(uint8_t *data, size_t length)
{
    int i;
    uint16_t crc = 0xffff;

    while (length--) {
        crc ^= *(unsigned char *)data++ << 8;
        for (i = 0; i < 8; ++i)
            crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc & 0xffff;
}

static int ant_antplus_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xAA};
    uint8_t b1[17], b2[17]; // aligned packet data for both preambles/offsets
    unsigned bit_offset;

    data_t *data;

    // validate buffer: ANT messages are shorter than 150us, i.e. ~140 bits at 1Mbps
    if (bitbuffer->bits_per_row[0] < 137 || bitbuffer->bits_per_row[0] > 160) {
        return DECODE_ABORT_LENGTH;
    }

    // find a data package and extract data buffer
    bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
    if (bit_offset + sizeof(b1) * 8 > bitbuffer->bits_per_row[0]) { // did not find a big enough package
        return DECODE_ABORT_LENGTH;
    }

    // ANT and ANT+ packes have either aa or 55 preamble, depending on the first bit of
    // the following byte. The best way to know which is being used, is to rely on the CRC
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b1, sizeof(b1) * 8);     // if preamble is AA
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset + 1, b2, sizeof(b2) * 8); // if preamble is 55, read starting 1 bit further right

    // calculate CRC for both alternatives (Schrodinger's CRC in superposition state)
    uint16_t crc1 = crc16ccitt(b1, 15);
    uint16_t crc2 = crc16ccitt(b2, 15);

    // all variables initialized to avoid bogus [-Wmaybe-uninitialized] warning
    // byte[6] value not decoded/displayed, it's always = 10
    uint16_t net_key    = 0;
    uint16_t id         = 0;
    uint8_t device_type = 0;
    uint8_t tx_type     = 0;
    char payload[8 * 3 + 1]; // payload is 8 hex pairs for ANT and ANT+

    // use CRC to decide if the data packet is using aa or 55 as preamble. Bytes 15 and 16 are the CRC
    // (peek into Schrodinger's box and collapse the CRC wavefunction)
    if (((b1[15] << 8) | b1[16]) == crc1) { // valid preamble is aa
        net_key     = (b1[1] << 8) | b1[0];
        id          = (b1[3] << 8) | b1[2];
        device_type = b1[4];
        tx_type     = b1[5];
        sprintf(payload, "%02x %02x %02x %02x %02x %02x %02x %02x", b1[7], b1[8], b1[9], b1[10], b1[11], b1[12], b1[13], b1[14]);

        // display ANT or ANT+ depending on the network key used. ANT+ only uses aa preamble
        char model[5] = "ANT\0";
        if (0xc5a6 == net_key)
            strncpy(model, "ANT+\0", 5);

        /* clang-format off */
        data = data_make(
                "model",       "",               DATA_STRING, model,
                "net_key",     "Net key",        DATA_FORMAT, "0x%04x", DATA_INT, net_key,
                "device_num",  "Device #",       DATA_FORMAT, "0x%04x", DATA_INT, id,
                "device_type", "Device type",    DATA_INT,    device_type,
                "tx_type",     "TX type",        DATA_INT,    tx_type,
                "payload",     "Payload",        DATA_STRING, payload,
                "CRC",         "CRC",            DATA_FORMAT, "0x%04x", DATA_INT, crc1,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else if (((b2[15] << 8) | b2[16]) == crc2) { // valid preamble is 55
        net_key     = (b2[1] << 8) | b2[0];
        id          = (b2[3] << 8) | b2[2];
        device_type = b2[4];
        tx_type     = b2[5];
        sprintf(payload, "%02x %02x %02x %02x %02x %02x %02x %02x", b2[7], b2[8], b2[9], b2[10], b2[11], b2[12], b2[13], b2[14]);

        /* clang-format off */
        data = data_make(
                "model",       "",               DATA_STRING, "ANT",
                "net_key",     "Net key",        DATA_FORMAT, "0x%04x", DATA_INT, net_key,
                "device_num",  "Device #",       DATA_FORMAT, "0x%04x", DATA_INT, id,
                "device_type", "Device type",    DATA_INT,    device_type,
                "tx_type",     "TX type",        DATA_INT,    tx_type,
                "payload",     "Payload",        DATA_STRING, payload,
                "CRC",         "CRC",            DATA_FORMAT, "0x%04x", DATA_INT, crc2,
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }
    else {
        return DECODE_FAIL_MIC;
    }
}

static char *output_fields[] = {
        "model",
        "net_key",
        "device_num",
        "device_type",
        "tx_type",
        "payload",
        "CRC",
        NULL,
};

r_device ant_antplus = {
        .name        = "ANT and ANT+ devices",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 1,
        .long_width  = 1,
        .sync_width  = 10,
        .gap_limit   = 500,
        .reset_limit = 500,
        .decode_fn   = &ant_antplus_decode,
        .fields      = output_fields,
};
