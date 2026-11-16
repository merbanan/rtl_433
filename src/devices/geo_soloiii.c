/** @file
    GEO SoloIII energy monitor.

    Copyright (C) 2026 Adam Stanbridge, adam at bitfields dot co dot uk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/** @fn int soloiii_decode(r_device *decoder, bitbuffer_t *bitbuffer)
GEO Solo III energy monitor.

@sa geo_minim_ct_sensor_decode()
@sa geo_solo3_a_sensor_decode()

This implementation is based on the Geo minim+ decoder.

The GEO minim+ energy monitor comprises a radio unit built in to the solar
generation meter, a display unit, and a network hub.
https://assets.geotogether.com/sites/4/20170719152416/SOL3-A-USG-001_3.pdf

The sensor unit is installed in the generation meter and pairs with the hub.
The display unit connects to the hub and obtains readings from there.
The hub is also used to transmit readings to GeoTogether's cloud service.
The cloud service is no longer active - hence the reason for this
implementation.

I believe the transmission of the raw data that this device is the stream of
data sent by the the generation meter sensor to the hub. It appears that the
hub and display do not need to be active for this to work but I don't know if
the meter sensor will stop sending if the hub and display are disconnected for
an extended time.

There is a pairing procedure to connect to the meter. I don't know if the id
in this decoder is specific to the pairing or the specific devices involved.

Frequency 868.29 MHz, bit period 25 microseconds (40kbps), modulation FSK_PCM

The display unit requires a 5V supply, provided by the supplied mains/USB
adapter. When paired the display listens for sensor packets and then transmits a
summary packet using the same protocol.

Packet types:

The first three header bytes are not identified but should be related to
message type, session ID from pairing. Seen so far:

    a1 00 7b 05 // GEO Solo III current sensor
    a1 00 7b 1d // GEO Solo III display

Then a byte of packet length 0x05 or 0x1d follows.

The following Flex decoder will capture the raw data:

    rtl_433 -f 868.29M -s 1024k -Y classic -X 'n=solo3,m=FSK_PCM,s=24,l=24,r=3000,preamble=0x7bb9'
*/

#include <time.h>

#include "decoder.h"


// packet type magic numbers
#define MLEN_DISPLAY 0x2a
#define MLEN_CT 0x05
#define MLEN_SOLO3_A 0x1d
#define MLEN_SOLO3_B 0x0b
#define MLEN_SOLO3_C 0x0a
#define MLEN_SOLO3_D 0x04

/**
GEO minim+ current sensor.

Packet layout:

- 24 bit preamble of alternating 0s and 1s
- 2 sync bytes: 0x7b 0xb9
- 3 byte header: contents unknown so far
- 1 byte packet length: 0x05
- 5 data bytes
- CRC16

The following Flex decoder will capture the raw sensor data:

    rtl_433 -f 868.29M -s 1024k -Y classic -X 'n=solo3,m=FSK_PCM,s=24,l=24,r=3000,preamble=0x7bb9a1'

Data format string:

    ID:24h VA:13d 3x UP:24d CRC:16h

    VA: Big endian power x10VA, bit14 = 5VA
    UP: Big endian uptime x9 seconds
*/
static int geo_minim_ct_sensor_decode(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t const buf[], unsigned len)
{
    (void)bitbuffer;

    if (buf[3] != 5) {
        decoder_logf_bitrow(decoder, 1, __func__, buf, 8 * len,
                "Incorrect length. Expected payload of 5 got %u bytes", len);
        return DECODE_ABORT_LENGTH;
    }

    if (len != 11) {
        decoder_logf_bitrow(decoder, 1, __func__, buf, 8 * len,
            "Incorrect length. Expected packet 11 got %u bytes", len);
        return DECODE_ABORT_LENGTH;
    }

    // char raw_values[23];
    // for (unsigned i = 0; i < len; i++) {
    //     snprintf(raw_values + (i * 2), 3, "%02X", buf[i]);
    // }
    // decoder_logf(decoder, 0, __func__, "RAW CT: %s", raw_values);

    char id[7];
    snprintf(id, sizeof(id), "%02X%02X%02X", buf[0], buf[1], buf[2]);

    // Uptime in ~8 second intervals
    unsigned uptime_raw = (buf[6] << 16) + (buf[7] << 8) + buf[8];
    unsigned uptime_s = 8 * uptime_raw;

    // Bytes 4 & 5 appear to be the instantaneous VA x10.
    // When scaled by the 'Fine Tune' setting (power factor [0.88]) set on the
    // display unit it matches the Watts value in display messages.
    unsigned va = 10 * (buf[5] + ((buf[4] & 0x0f) << 8));
    if (buf[4] & 0x40)
        va += 5;

    // TODO: what are the flag bits in buf[4] (0x30)? Battery OK, Fault?
    unsigned flags4 = buf[4] & ~0x4f;

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "GEO-minimCT",
            "id",           "",             DATA_STRING, id,
            "power_VA",     "Power",        DATA_FORMAT, "%u VA", DATA_INT, va,
            "flags4",       "Flags",        DATA_COND, flags4 != 0x30, DATA_FORMAT, "%#x", DATA_INT, flags4,
            "uptime_s",     "Uptime",       DATA_INT, uptime_s,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1; // Message successfully decoded
}

/**
GEO solo III display readings.

Packet layout:

- 24 bit preamble of alternating 0s and 1s
- 2 sync bytes: 0x7b 0xb9
- 3 byte header: contents unknown so far
- 1 byte packet length: 0x1d
- 29 data bytes
- CRC16

Example:
A1007B 1D48 4050 0359 00DA 05F6297A 023F586C 0494BB4E 00DDEA3F 6AF99ECC1FBD 7D12
Id               UP   GP   UE       GE       IE       EE                    CRC

   UP: 857
   GP: 218
   UE: 100,018,554
   GE: 37,705,836
   IE: 76,856,142
   EE: 14,543,423

The following Flex decoder will capture the raw sensor data:

    rtl_433 -f 868.29M -s 1024k -Y classic -X 'n=solo3,m=FSK_PCM,s=24,l=24,r=3000,preamble=0x7bb9a1'

Data format string:

    ID:24h 32x UP:16d GP: 16d UE: 32d GE: 32d IE: 32d EE: 32d 24x CRC:16h

    UP: Usage power (Watts) - little endian - uint16
    GP: Generation power (Watts) - little endian - uint16
    UE: Usage lifetime energy (Watt hours) - little endian - uint32
    GE: Generation lifetime energy (Watt hours) - little endian - uint32
    IE: Import lifetime energy (Watt hours) - little endian - uint32
    EE: Export lifetime energy (Watt hours) - little endian - uint32
*/

static int geo_solo3_a_decode(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t const buf[], unsigned len)
{
    (void)bitbuffer;

    if (buf[3] != MLEN_SOLO3_A) {
        decoder_logf_bitrow(decoder, 1, __func__, buf, 8 * len,
                "Incorrect length. Expected payload of %d got %u bytes", MLEN_SOLO3_B, len);
        return DECODE_ABORT_LENGTH;
    }

    if (len != 35) {
        decoder_logf_bitrow(decoder, 1, __func__, buf, 8 * len,
            "Incorrect length. Expected packet 35 got %u bytes", len);
        return DECODE_ABORT_LENGTH;
    }

    // char raw_values[71];
    // for (unsigned i = 0; i < len; i++) {
    //     snprintf(raw_values + (i * 2), 3, "%02X", buf[i]);
    // }
    // decoder_logf(decoder, 0, __func__, "RAW A: %s", raw_values);

    char id[7];
    snprintf(id, sizeof(id), "%02X%02X%02X", buf[0], buf[1], buf[2]);
    decoder_logf(decoder, 1, __func__, "id %s", id);

    // Instantanious Power (watts)
    unsigned usagePower = buf[8] + (buf[7] << 8);
    unsigned generatingPower = buf[10] + (buf[9] << 8);

    // Total Energy Used (watt hours)
    unsigned usageEnergy = buf[14] + (buf[13] << 8) + (buf[12] << 16) + ((unsigned)buf[11] << 24);
    unsigned generationEnergy = buf[18] + (buf[17] << 8) + (buf[16] << 16) + ((unsigned)buf[15] << 24);
    unsigned importEnergy = buf[22] + (buf[21] << 8) + (buf[20] << 16) + ((unsigned)buf[19] << 24);
    unsigned exportEnergy = buf[26] + (buf[25] << 8) + (buf[24] << 16) + ((unsigned)buf[23] << 24);

    /* clang-format off */
    data_t *data = data_make(
            "model",        "",             DATA_STRING, "GEO-SoloIII A",
            "id",           "",             DATA_STRING, id,
            "gPower_W",     "Live Gen",     DATA_FORMAT, "%u", DATA_INT, generatingPower,
            "uPower_W",     "Live Usage",   DATA_FORMAT, "%u", DATA_INT, usagePower,
            "uEnergy_Wh",   "Total Usage",  DATA_FORMAT, "%u", DATA_INT, usageEnergy,
            "gEnergy_Wh",   "Total Gen",    DATA_FORMAT, "%u", DATA_INT, generationEnergy,
            "iEnergy_Wh",   "Total Import", DATA_FORMAT, "%u", DATA_INT, importEnergy,
            "eEnergy_Wh",   "Total Export", DATA_FORMAT, "%u", DATA_INT, exportEnergy,
            "mic",          "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1; // Message successfully decoded
}

static int soloiii_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // preamble and sync can be aaaa7bb9 or 55557bb9
    uint8_t const preamble1[] = { 0xaa, 0xaa, 0x7b, 0xb9 };
    uint8_t const preamble2[] = { 0x55, 0x55, 0x7b, 0xb9 };
    const unsigned preamble_len = 8 * sizeof(preamble1);

    if (bitbuffer->num_rows != 1) {
        return DECODE_ABORT_LENGTH;
    }

    unsigned row = 0; // we expect only one row

    // Search preamble+sync, try alternative
    unsigned bitpos = bitbuffer_search(bitbuffer, row, 0, preamble1, preamble_len) + preamble_len;
    if (bitpos >= bitbuffer->bits_per_row[row]) {
        bitpos = bitbuffer_search(bitbuffer, row, 0, preamble2, preamble_len) + preamble_len;
    }
    if (bitpos >= bitbuffer->bits_per_row[row]) {
        decoder_logf_bitbuffer(decoder, 3, __func__, bitbuffer, "Sync not found");
        return DECODE_ABORT_EARLY;
    }

    unsigned bits = bitbuffer->bits_per_row[row];

    // Extract frame header
    unsigned const hdr_len = 4;
    unsigned const hdr_bits = hdr_len * 8;
    if (bitpos + hdr_bits >= bits)
        return DECODE_ABORT_LENGTH;

    bits -= bitpos;
    uint8_t buf[128];
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, buf, hdr_bits);

    // Determine frame type based on packet length
    int data_length = buf[3];
    if (data_length != MLEN_DISPLAY && data_length != MLEN_CT && data_length != MLEN_SOLO3_A && 
        data_length != MLEN_SOLO3_B && data_length != MLEN_SOLO3_C && data_length != MLEN_SOLO3_D) {
        decoder_logf(decoder, 1, __func__,
                "Unknown header %02x%02x%02x%02x",
                buf[0], buf[1], buf[2], buf[3]);
        return DECODE_ABORT_EARLY;
    }

    unsigned bytes = bits / 8;
    unsigned maxlen = sizeof(buf);
    if (bytes > maxlen) {
        decoder_logf(decoder, 1, __func__,
                "Too big: %u > %u max bytes", bits / 8, maxlen);
        //return DECODE_ABORT_LENGTH;
        bytes = maxlen;
    }

    // Check offset to crc16 using data_len @ header[3]
    unsigned crc_len = hdr_len + buf[3];
    if (crc_len + 2 > bytes) {
        decoder_logf(decoder, 1, __func__,
                "Truncated - got %u of %u bytes", bytes, crc_len + 2);
        return DECODE_FAIL_SANITY;
    }

    // Extract byte-aligned data
    bitbuffer_extract_bytes(bitbuffer, row, bitpos + hdr_bits, buf + hdr_len, (bytes - hdr_len) * 8);

    // Message Integrity Check
    unsigned crc = crc16(buf, crc_len, 0x8005, 0);
    unsigned crc_rcvd = (buf[crc_len] << 8) | buf[crc_len + 1];
    if (crc != crc_rcvd) {
        decoder_logf_bitrow(decoder, 1, __func__, buf, (crc_len + 2) * 8,
                "Bad CRC. Expected %04X got %04X", crc, crc_rcvd);
        return DECODE_FAIL_MIC;
    }

    if (data_length == MLEN_CT) {
        // SoloIII also provides the same CT sensor reading as the Minim+
        return geo_minim_ct_sensor_decode(decoder, bitbuffer, buf, bytes);
    }
    if (data_length == MLEN_SOLO3_A) {
        return geo_solo3_a_decode(decoder, bitbuffer, buf, bytes);
    }
    if ((data_length == MLEN_SOLO3_B) ||
        (data_length == MLEN_SOLO3_C) ||
        (data_length == MLEN_SOLO3_D))
    {
        // Unused
        return 1;
    }

    return DECODE_FAIL_SANITY;
}

// List of fields to appear in the `-F csv` output.
static char const *const output_fields[] = {
        "model",
        "id",
        "power_VA",
        "flags4",
        "uptime_s",
        "gPower_W",
        "uPower_W",
        "uEnergy_Wh",
        "gEnergy_Wh",
        "iEnergy_Wh",
        "eEnergy_Wh",
        "mic",
        NULL,
};

r_device const geo_soloiii = {
        .name           = "GEO Solo III solor energy monitor",
        .modulation     = FSK_PULSE_PCM,
        .short_width    = 24,
        .long_width     = 24,
        .reset_limit    = 3000,
        .decode_fn      = &soloiii_decode,
        .fields         = output_fields,
};
