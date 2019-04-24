/* OpenEnergyMonitor.org emonTx sensor protocol
 *
 * Copyright (C) 2016 Tommy Vestermark
 * Copyright (C) 2016 David Woodhouse
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

// This is the JeeLibs RF12 packet format as described at
// http://jeelabs.org/2011/06/09/rf12-packet-format-and-design/
//
// The RFM69 chip misses out the zero bit at the end of the
// 0xAA 0xAA 0xAA preamble; the receivers only use it to set
// up the bit timing, and they look for the 0x2D at the start
// of the packet. So we'll do the same — except since we're
// specifically looking for emonTx packets, we can require a
// little bit more. We look for a group of 0xD2, and we
// expect the CDA bits in the header to all be zero:
static unsigned char preamble[3] = { 0xaa, 0xaa, 0xaa };
static unsigned char pkt_hdr_inverted[3] = { 0xd2, 0x2d, 0xc0 };
static unsigned char pkt_hdr[3] = { 0x2d, 0xd2, 0x00 };

static int emontx_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
	bitrow_t *bb = bitbuffer->bb;
	unsigned bitpos = 0;
	unsigned bits = bitbuffer->bits_per_row[0];
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
		double vrms;
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
				continue;
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
			continue;
		crc = crc16lsb((uint8_t *)&pkt.p.group, 0x1d, 0xa001, 0xffff);

		// Ick. If we could just do le16_to_cpu(pkt.p.ct1) we wouldn't need this.
		for (i=0; i<14; i++)
			words[i] = pkt.b[4 + (i * 2)] | (pkt.b[5 + i * 2] << 8);
		if (crc != words[13])
			continue;

		vrms = (double)words[4] / 100.0;

		data = data_make(
				 "model", "", DATA_STRING, _X("emonTx-Energy","emonTx"),
				 "node", "", DATA_FORMAT, "%02x", DATA_INT, pkt.p.node & 0x1f,
				 "ct1", "", DATA_FORMAT, "%d", DATA_INT, (int16_t)words[0],
				 "ct2", "", DATA_FORMAT, "%d", DATA_INT, (int16_t)words[1],
				 "ct3", "", DATA_FORMAT, "%d", DATA_INT, (int16_t)words[2],
				 "ct4", "", DATA_FORMAT, "%d", DATA_INT, (int16_t)words[3],
				 _X("batt_Vrms","Vrms/batt"), "", DATA_FORMAT, "%.2f", DATA_DOUBLE, vrms,
				 "pulse", "", DATA_FORMAT, "%u", DATA_INT, words[11] | ((uint32_t)words[12] << 16),
				 // Slightly horrid... a value of 300.0°C means 'no reading'. So omit them completely.
				 words[5] == 3000 ? NULL : "temp1_C", "", DATA_FORMAT, "%.1f", DATA_DOUBLE, (double)words[5] / 10.0,
				 words[6] == 3000 ? NULL : "temp2_C", "", DATA_FORMAT, "%.1f", DATA_DOUBLE, (double)words[6] / 10.0,
				 words[7] == 3000 ? NULL : "temp3_C", "", DATA_FORMAT, "%.1f", DATA_DOUBLE, (double)words[7] / 10.0,
				 words[8] == 3000 ? NULL : "temp4_C", "", DATA_FORMAT, "%.1f", DATA_DOUBLE, (double)words[8] / 10.0,
				 words[9] == 3000 ? NULL : "temp5_C", "", DATA_FORMAT, "%.1f", DATA_DOUBLE, (double)words[9] / 10.0,
				 words[10] == 3000 ? NULL : "temp6_C", "", DATA_FORMAT, "%.1f", DATA_DOUBLE, (double)words[10] / 10.0,
				 NULL);
		decoder_output_data(decoder, data);
		events++;
	}
	return events;
}

static char *output_fields[] = {
	"model",
	"node",
	"ct1",
	"ct2",
	"ct3",
	"ct4",
	"Vrms/batt", // TODO: delete this
	"batt_Vrms",
	"temp1_C",
	"temp2_C",
	"temp3_C",
	"temp4_C",
	"temp5_C",
	"temp6_C",
	"pulse",
	NULL
};

r_device emontx = {
	.name           = "emonTx OpenEnergyMonitor",
	.modulation     = FSK_PULSE_PCM,
	.short_width    = 2000000.0f / (49230 + 49261), // 49261kHz for RFM69, 49230kHz for RFM12B
	.long_width     = 2000000.0f / (49230 + 49261),
	.reset_limit    = 1200,	// 600 zeros...
	.decode_fn      = &emontx_callback,
	.disabled       = 0,
	.fields		= output_fields,
};
