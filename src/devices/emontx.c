/** @file
    OpenEnergyMonitor.org emonTx sensor protocol.

    Copyright (C) 2016 Tommy Vestermark
    Copyright (C) 2016 David Woodhouse

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

// We don't really *use* this because there's no endianness support
// for just using le16_to_cpu(pkt.ct1) etc. A task for another day...
#pragma pack(push, 1)
struct emontx {
    uint8_t syn, group, node, len;
    uint16_t ct1, ct2, ct3, ct4, Vrms, temp[6];
    uint32_t pulse;
    uint16_t crc;
    uint8_t postamble;
};
#pragma pack(pop)

/** @fn int emontx_callback(r_device *decoder, bitbuffer_t *bitbuffer)
OpenEnergyMonitor.org emonTx sensor protocol.

This is the JeeLibs RF12 packet format as described at
http://jeelabs.org/2011/06/09/rf12-packet-format-and-design/

The RFM69 chip misses out the zero bit at the end of the
0xAA 0xAA 0xAA preamble; the receivers only use it to set
up the bit timing, and they look for the 0x2D at the start
of the packet. So we'll do the same -- except since we're
specifically looking for emonTx packets, we can require a
little bit more. We look for a group of 0xD2, and we
expect the CDA bits in the header to all be zero:
*/
static unsigned char const preamble[3] = { 0xaa, 0xaa, 0xaa };
static unsigned char const pkt_hdr_inverted[3] = { 0xd2, 0x2d, 0xc0 };
static unsigned char const pkt_hdr[3] = { 0x2d, 0xd2, 0x00 };

static int emontx_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    unsigned bitpos = 0;
    int events = 0;

    // Search for only 22 bits to cope with inverted frames and
    // the missing final preamble bit with RFM69 transmissions.
    while ((bitpos = bitbuffer_search(bitbuffer, 0, bitpos,
                      preamble, 22)) < bitbuffer->bits_per_row[0]) {
        int inverted = 0;
        unsigned pkt_pos;
        uint16_t crc;
        data_t *data;
        union {
            struct emontx p;
            uint8_t b[sizeof(struct emontx)];
        } pkt;
        uint16_t words[14];
        float vrms;
        unsigned i;

        bitpos += 22;

        // Eat any additional 101010 sequences (which might be attributed
        // to noise at the start of the packet which coincidentally matches).
        while (bitbuffer_search(bitbuffer, 0, bitpos, preamble, 2) == bitpos)
            bitpos += 2;

        // Account for RFM69 bug which drops a zero bit at the end of the
        // preamble before the 0x2d SYN byte. And for inversion.
        bitpos--;

        // Check for non-inverted packet header...
        pkt_pos = bitbuffer_search(bitbuffer, 0, bitpos,
                       pkt_hdr, 11);

        // And for inverted, if it's not found close enough...
        if (pkt_pos > bitpos + 5) {
            pkt_pos = bitbuffer_search(bitbuffer, 0, bitpos,
                           pkt_hdr_inverted, 11);
            if (pkt_pos > bitpos + 5)
                continue; // DECODE_ABORT_EARLY
            inverted = 1;
        }

        // Need enough data for a full packet (including postamble)
        if (pkt_pos + sizeof(pkt)*8 > bitbuffer->bits_per_row[0])
            break;

        // Extract the group even though we matched on it; the CRC
        // covers it too. And might as well have the 0x2d too for
        // alignment.
        bitbuffer_extract_bytes(bitbuffer, 0, pkt_pos,
                    (uint8_t *)&pkt, sizeof(pkt) * 8);
        if (inverted) {
            for (i=0; i<sizeof(pkt); i++)
                pkt.b[i] ^= 0xff;
        }
        if (pkt.p.len != 0x1a || pkt.p.postamble != 0xaa)
            continue; // DECODE_ABORT_EARLY
        crc = crc16lsb((uint8_t *)&pkt.p.group, 0x1d, 0xa001, 0xffff);

        // Ick. If we could just do le16_to_cpu(pkt.p.ct1) we wouldn't need this.
        for (i=0; i<14; i++)
            words[i] = pkt.b[4 + (i * 2)] | (pkt.b[5 + i * 2] << 8);
        if (crc != words[13])
            continue; // DECODE_FAIL_MIC

        vrms = (float)words[4] / 100.0f;

        /* clang-format off */
        data = NULL;
        data = data_str(data, "model",        "",             NULL,         "emonTx-Energy");
        data = data_int(data, "node",         "",             "%02x",       pkt.p.node & 0x1f);
        data = data_int(data, "ct1",          "",             "%d",         (int16_t)words[0]);
        data = data_int(data, "ct2",          "",             "%d",         (int16_t)words[1]);
        data = data_int(data, "ct3",          "",             "%d",         (int16_t)words[2]);
        data = data_int(data, "ct4",          "",             "%d",         (int16_t)words[3]);
        data = data_dbl(data, "batt_Vrms",    "",             "%.2f",       vrms);
        data = data_int(data, "pulse",        "",             "%u",         words[11] | ((uint32_t)words[12] << 16));
                // Slightly horrid... a value of 300.0°C means 'no reading'. So omit them completely.
        if (words[5] != 3000) {
            data = data_dbl(data, "temp1_C",      "",             "%.1f",       words[5] * 0.1f);
        }
        if (words[6] != 3000) {
            data = data_dbl(data, "temp2_C",      "",             "%.1f",       words[6] * 0.1f);
        }
        if (words[7] != 3000) {
            data = data_dbl(data, "temp3_C",      "",             "%.1f",       words[7] * 0.1f);
        }
        if (words[8] != 3000) {
            data = data_dbl(data, "temp4_C",      "",             "%.1f",       words[8] * 0.1f);
        }
        if (words[9] != 3000) {
            data = data_dbl(data, "temp5_C",      "",             "%.1f",       words[9] * 0.1f);
        }
        if (words[10] != 3000) {
            data = data_dbl(data, "temp6_C",      "",             "%.1f",       words[10] * 0.1f);
        }
        data = data_str(data, "mic",          "Integrity",    NULL,         "CRC");
        /* clang-format on */
        decoder_output_data(decoder, data);
        events++;
    }
    return events;
}

static char const *const output_fields[] = {
        "model",
        "node",
        "ct1",
        "ct2",
        "ct3",
        "ct4",
        "batt_Vrms",
        "temp1_C",
        "temp2_C",
        "temp3_C",
        "temp4_C",
        "temp5_C",
        "temp6_C",
        "pulse",
        "mic",
        NULL,
};

r_device const emontx = {
        .name        = "emonTx OpenEnergyMonitor",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 2000000.0f / (49230 + 49261), // 49261kHz for RFM69, 49230kHz for RFM12B
        .long_width  = 2000000.0f / (49230 + 49261),
        .reset_limit = 1200, // 600 zeros...
        .decode_fn   = &emontx_callback,
        .fields      = output_fields,
};
