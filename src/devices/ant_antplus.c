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

ANT and ANT+ communication standards are defined by a division of Garmin
https://www.thisisant.com/ and used widely for low power devices.
The ANT radio transmits for less than 150 µs per message, allowing a single
channel to be divided into hundreds of time slots and avoiding collisions.
ANT and ANT+ devices use a modified Shockburst protocol, in the 2.4GHz ISM band,
with 160kHz deviation and 1Mbps data rates, GFSK encoded. The low level
layer is not documented anywhere. ANT chips use an 8 byte key to generate a 2 byte
network ID using an unspecified algorithm. Valid keys are only assigned by Garmin
and require specific licensing terms (and in some cases a payment)
ANT+ uses the basic ANT message structure but is a managed network with a specific
network key and defined device types, each sending "data pages" of 8 bytes with
specific data for the device type. Most ANT+ devices are sports focused like
heart rate monitors, bicycle sensors, or environmental sensors.

Please note that unlike most devices in the rtl_433 repository, ANT+ devices
operate in the ISM band between 2.4GHz and 2.5GHz. Decoding these signals
requires an SDR capable of operating above 2.4GHz (e.g. PlutoSDR) or the
use of a downconverter for rtl_sdr. The ISM band is very noisy, so it's
recommended to get the device very close, use only a ~30mm wire antenna and
use mid-low gains. Finally, since the protocol encodes at 1Mbps, sampling
rate should be -s 4M or higher. It can work with a sampling rate as low as
2Msps, but unreliably. To avoid excessive warnings when running with default
250k sampling rate, the decoder is disabled by default.

The following works well with PlutoSDR:

rtl_433 -d driver=plutosdr,uri=ip:192.168.2.1 -g 20 -f 2457.025M -s 4M

Without knowing the 8 byte key, existing ANT chips do not allow sniffing
of the data packets. Hence the need for this decoder. At the moment it's
not possible to recover the ANT key from the 16 bit on-air network key.

This decoder only captures and displays the low-level packets, identifying the
2 byte network key plus all other device characteristics and 8 byte ANT payload.
It identifies ANT+ packets using the unique network key used (0xa6c5)
Refer to ANT+ documentation for ech specific device to parse the content of
the pages sent as 8 byte ANT+ payload.

The ANT protocol is using an uncommon strategy for the preamble: either 0x55 or
0xaa, depending on the value of the first bit of the following byte (0 and 1
respectively). The nRF24L01, on which the ANT protocol is based, uses the same
preamble strategy. In order to determine if the packet is using 0x55 or 0xaa,
both packets are extracted and the only valid one is determined by matching
the packet CRC with a calculated CRC value for both alternative packets.

The payload is 18 bytes long structured as follows:

    PNNDDTXLPPPPPPPPCC

- P: Preamble: either 0x55 or 0xAA, depending on the value of first bit of the next byte
- N: Network key, assume LSB first (ANT+ uses 0xc5a6, most invalid keys 0x255b)
- D: Device number, 16 bit. LSB first
- X: Transmission type
- L: ANT payload length including CRC
- P: 8 byte ANT or ANT+ payload
- C: 16 bit CRC (CRC-16/CCITT-FALSE)

Weirdly, L is always 10, because at the moment the ANT payload is always 8 bytes

CREDITS:
https://github.com/sghctoma/antfs-poc-defcon24
https://reveng.sourceforge.io/ to reverse engineer the CRC algorithm used
*/

#include "decoder.h"

static int ant_antplus_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xAA};
    uint8_t b[17]; // aligned packet data for both preambles/offsets
    unsigned bit_offset;
    int antplus_flag = 0;

    // validate buffer: ANT messages are shorter than 150us, i.e. ~140 bits at 1Mbps
    if (bitbuffer->bits_per_row[0] < 120 || bitbuffer->bits_per_row[0] > 200) {
        return DECODE_ABORT_LENGTH;
    }

    // find a data package and extract data buffer
    bit_offset = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble) * 8) + sizeof(preamble) * 8;
    if (bit_offset + sizeof(b) * 8 > bitbuffer->bits_per_row[0]) { // did not find a big enough package
        return DECODE_ABORT_LENGTH;
    }

    // ANT and ANT+ packets have either aa or 55 preamble, depending on the first bit of
    // the following byte. i.e. 10101010 1xxxxxxx or 01010101 0xxxxxxx
    // the best way to know which is being used, is to verify which one has a valid CRC
    // the following code relies on the fact that 55 is aa shifted right
    bitbuffer_extract_bytes(bitbuffer, 0, bit_offset, b, sizeof(b) * 8); // assume preamble is aa

    // calculate CRC for both alternatives, starting with aa (used by all ANT+ devices)
    // if the two crc bytes b[15] and b[16] are included in the crc calculation, a valid packet returns 0
    if (crc16(b, 17, 0x1021, 0xffff) != 0) { // no, preamble is not aa
        bitbuffer_extract_bytes(bitbuffer, 0, bit_offset + 1, b, sizeof(b) * 8); // shift one bit right for 55 preamble
        if (crc16(b, 17, 0x1021, 0xffff) != 0) // nope, not preamble = 55 either, invalid packet, abort
            return DECODE_FAIL_MIC;
    }

    uint16_t net_key    = (b[1] << 8) | b[0]; // undocumented, assume it's LSB first, as the device id
    uint16_t id         = (b[3] << 8) | b[2]; // id is always LSB first
    uint8_t device_type = b[4];
    uint8_t tx_type     = b[5];
    // display ANT and ANT+ payload in the same format used by ANT tools
    char payload[8 * 3 + 1]; // payload is 8 hex pairs for ANT and ANT+
    snprintf(payload, sizeof(payload), "%02x %02x %02x %02x %02x %02x %02x %02x", b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14]);

    // display ANT or ANT+ depending on the network key used.
    if (0xc5a6 == net_key)
        antplus_flag = 1;

    /* clang-format off */
    data_t *data = data_make(
        "model",       "",               DATA_STRING, "Garmin-ANT",
        "network",     "Network",        DATA_COND,   antplus_flag == 1,  DATA_STRING, "ANT+",
        "network",     "Network",        DATA_COND,   antplus_flag == 0,  DATA_STRING, "ANT",
        "channel",     "Net key",        DATA_FORMAT, "0x%04x", DATA_INT, net_key,
        "id",          "Device #",       DATA_FORMAT, "0x%04x", DATA_INT, id,
        "device_type", "Device type",    DATA_INT,    device_type,
        "tx_type",     "TX type",        DATA_INT,    tx_type,
        "payload",     "Payload",        DATA_STRING, payload,
        "mic",         "Integrity",      DATA_STRING, "CRC",
        NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
        "model",
        "network",
        "channel",
        "id",
        "device_type",
        "tx_type",
        "payload",
        "mic",
        NULL,
};

r_device const ant_antplus = {
        .name        = "ANT and ANT+ devices",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 1,
        .long_width  = 1,
        .sync_width  = 8,
        .gap_limit   = 500,
        .reset_limit = 500,
        .decode_fn   = &ant_antplus_decode,
        .fields      = output_fields,
        .disabled    = 1, // disabled by default, because of higher than default sampling requirements (s = 4M)
};
