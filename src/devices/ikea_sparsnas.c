/* Ambient Weather TX-8300 (also sold as TFA 30.3211.02)
 * Copyright (C) 2018 ionum-projekte and Roger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 * 1970us pulse with variable gap (third pulse 3920 us)
 * Above 79% humidity, gap after third pulse is 5848 us
 *
 * Bit 1 : 1970us pulse with 3888 us gap
 * Bit 0 : 1970us pulse with 1936 us gap
 *
 * 74 bit (2 bit preamble and 72 bit data => 9 bytes => 18 nibbles)
 * The preamble seems to be a repeat counter (00, and 01 seen),
 * the first 4 bytes are data,
 * the second 4 bytes the same data inverted,
 * the last byte is a checksum.
 *
 * Preamble format (2 bits):
 *  [1 bit (0)] [1 bit rolling count]
 *
 * Payload format (32 bits):
 *   HHHHhhhh ??CCNIII IIIITTTT ttttuuuu
 *     H = First BCD digit humidity (the MSB might be distorted by the demod)
 *     h = Second BCD digit humidity, invalid humidity seems to be 0x0e
 *     ? = Likely battery flag, 2 bits
 *     C = Channel, 2 bits
 *     N = Negative temperature sign bit
 *     I = ID, 7-bit
 *     T = First BCD digit temperature
 *     t = Second BCD digit temperature
 *     u = Third BCD digit temperature
 *
 * The Checksum seems to cover the data bytes and is roughly something like:
 *
 *  = (buffer[0] & 0x5) + (b[0] & 0xf) << 4  + (b[0] & 0x50) >> 4 + (b[0] & 0xf0)
 *  + (b[1] & 0x5) + (b[1] & 0xf) << 4  + (b[1] & 0x50) >> 4 + (b[1] & 0xf0)
 *  + (b[2] & 0x5) + (b[2] & 0xf) << 4  + (b[2] & 0x50) >> 4 + (b[2] & 0xf0)
 *  + (b[3] & 0x5) + (b[3] & 0xf) << 4  + (b[3] & 0x50) >> 4 + (b[3] & 0xf0)
 */

#include "decoder.h"

#define IKEA_SPARSNAS_SENSOR_ID 617633
#define IKEA_SPARSNAS_PULSES_PER_KWH 1000
#define IKEA_SPARSNAS_MESSAGE_BITLEN 160    // 20 bytes incl 8 bit length, 8 bit address, 128 bits data, and 16 bits of CRC. Excluding preamble and sync word

/*
Packet structure according to: https://github.com/strigeus/sparsnas_decoder
0: uint8_t length;        // Always 0x11
1: uint8_t sender_id_lo;  // Lowest byte of sender ID
2: uint8_t unknown;       // Not sure
3: uint8_t major_version; // Always 0x07 - the major version number of the sender.
4: uint8_t minor_version; // Always 0x0E - the minor version number of the sender.
5: uint32_t sender_id;    // ID of sender
9: uint16_t time;         // Time in units of 15 seconds.
11:uint16_t effect;       // Current effect usage
13:uint32_t pulses;       // Total number of pulses
17:uint8_t battery;       // Battery level, 0-100.
uint16_t CRC
*/

#define IKEA_SPARSNAS_MESSAGE_BYTELEN    (IKEA_SPARSNAS_MESSAGE_BITLEN + 7) / 8

#define IKEA_SPARSNAS_PREAMBLE_BITLEN 32
static const uint8_t preamble_pattern[4] = {0xAA, 0xAA, 0xD2, 0x01};


// Graciously copied from https://github.com/strigeus/sparsnas_decoder
static uint16_t ikea_sparsnas_crc16(const uint8_t *data, size_t n) {
  uint16_t crcReg = 0xffff;
  size_t i, j;
  for (j = 0; j < n; j++) {
    uint8_t crcData = data[j];
    for (i = 0; i < 8; i++) {
      if (((crcReg & 0x8000) >> 8) ^ (crcData & 0x80))
        crcReg = (crcReg << 1) ^ 0x8005;
      else
        crcReg = (crcReg << 1);
      crcData <<= 1;
    }
  }
  return crcReg;
}

static int ikea_sparsnas_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{

    if (decoder->verbose > 1)
        decoder_output_bitbufferf(decoder, bitbuffer, "IKEA Sparsnäs:");
  

    uint8_t buffer[IKEA_SPARSNAS_MESSAGE_BYTELEN];
    
    uint16_t bitpos = bitbuffer_search(bitbuffer, 0, 0, (const uint8_t *)&preamble_pattern, IKEA_SPARSNAS_PREAMBLE_BITLEN);

    if (!bitpos || (bitpos + IKEA_SPARSNAS_MESSAGE_BITLEN > bitbuffer->bits_per_row[0])) {
        if (decoder->verbose > 1)
            fprintf(stderr, "IKEA Sparsnäs: malformed package, preamble not found. (Expected 0xAAAAD201)\n");
        return 0;
    }
    
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos + IKEA_SPARSNAS_PREAMBLE_BITLEN, buffer, IKEA_SPARSNAS_MESSAGE_BITLEN);
    
    if (decoder->verbose > 1)
        decoder_output_bitrowf(decoder, buffer, IKEA_SPARSNAS_MESSAGE_BITLEN, "Encrypted message");

    if (decoder->verbose > 1)
        fprintf(stderr, "IKEA Sparsnäs: Found a message at bitpos %3d\n", bitpos);


    uint16_t crc_calculated = ikea_sparsnas_crc16(buffer, IKEA_SPARSNAS_MESSAGE_BYTELEN - 2);
    uint16_t crc_received = buffer[18] << 8 | buffer[19];

    if (crc_received != crc_calculated) {
        if (decoder->verbose > 1)
            fprintf(stderr, "IKEA Sparsnäs: CRC check failed (0x%X != ox%X)\n", crc_calculated, crc_received);
        return 0;
    }

    if (decoder->verbose > 1)
        fprintf(stderr, "IKEA Sparsnäs: CRC OK (%X == %X)\n", crc_calculated, crc_received);

    uint8_t decrypted[18];

    uint8_t key[5];
    const uint32_t sensor_id_sub = IKEA_SPARSNAS_SENSOR_ID - 0x5D38E8CB;

    key[0] = (uint8_t)(sensor_id_sub >> 24);
    key[1] = (uint8_t)(sensor_id_sub);
    key[2] = (uint8_t)(sensor_id_sub >> 8);
    key[3] = 0x47;
    key[4] = (uint8_t)(sensor_id_sub >> 16);
    
    if (decoder->verbose > 1)
        fprintf(stderr, "IKEA Sparsnäs: Encryption key: 0x%X%X%X%X%X\n", key[0], key[1], key[2], key[3], key[4]);
    
    for (size_t i = 0; i < 5; i++)
        decrypted[i] = buffer[i];

    for(size_t i = 0; i < 13; i++)
        decrypted[5 + i] = buffer[5 + i] ^ key[i % 5];

    if (decoder->verbose > 1)
        decoder_output_bitrowf(decoder, decrypted, 18 * 8, "Decrypted");

    int rcv_sensor_id = decrypted[5] << 24 | decrypted[6] << 16 | decrypted[7] << 8 | decrypted[8];

    if (rcv_sensor_id != IKEA_SPARSNAS_SENSOR_ID) {
        if (decoder->verbose > 1)
            fprintf(stderr, "IKEA Sparsnäs: Received sensor id (%d) not the same as sender (%d)\n", rcv_sensor_id, IKEA_SPARSNAS_SENSOR_ID);
        return 0;
    }

    if (decrypted[0] != 0x11){
        decoder_output_bitrowf(decoder, decrypted + 5, 13 * 8,  "Message malformed");
        if (decoder->verbose > 1)
            fprintf(stderr, "IKEA Sparsnäs: Message malformed (byte0=%X expected %X)\n", decrypted[0], 0x11);
        return 0;
    }
    
    if (decrypted[3] != 0x07){
        decoder_output_bitrowf(decoder, decrypted + 5, 13 * 8,  "Message malformed");
        if (decoder->verbose > 1)
            fprintf(stderr, "IKEA Sparsnäs: Message malformed (byte3=%X expected %X)\n", decrypted[0], 0x07);
        return 0;
    }

    if (decoder->verbose > 1)
        fprintf(stderr, "IKEA Sparsnäs: Received sensor id: %d\n", rcv_sensor_id);
    
    uint16_t sequence_number = (decrypted[9] << 8 | decrypted[10]);
    uint16_t effect = (decrypted[11] << 8 | decrypted[12]);
    uint32_t pulses = (decrypted[13] << 24 | decrypted[14] << 16 | decrypted[15] << 8 | decrypted[16]);
    uint8_t battery = decrypted[17];
    double watt = effect * 24;
    uint8_t mode = decrypted[4]^0x0f;

    //Note that mode cycles between 0-3 when you first put in the batteries in
    if(mode == 1){
      watt = (double)((3600000 / IKEA_SPARSNAS_PULSES_PER_KWH) * 1024) / (effect);
    } else if (mode == 0 ) { // special mode for low power usage
      watt = effect * 0.24 / IKEA_SPARSNAS_PULSES_PER_KWH;
    }
    
    double cumulative_kWh = ((double)pulses) / ((double)IKEA_SPARSNAS_PULSES_PER_KWH);

    
    data_t *data;
    data = data_make(
        "model",         "Model",               DATA_STRING, "IKEA Sparsnäs",
        "id",            "Sensor ID",           DATA_INT, rcv_sensor_id,
        "sequence",      "Sequence Number",     DATA_INT, sequence_number,
        "battery",       "Battery",             DATA_FORMAT, "%d%%", DATA_INT, battery,
        "cumulative_kWh", "Cumulative kWh",     DATA_FORMAT, "%7.3fkWh", DATA_DOUBLE,  cumulative_kWh,
        "effect",        "Effect",              DATA_FORMAT, "%dW", DATA_INT,  effect,
        "pulses",        "Pulses",              DATA_INT,  pulses,
        "mode",          "Mode",                DATA_INT, mode,
        NULL
    );

    decoder_output_data(decoder, data);
    
    return 1;


}

static char *output_fields[] = {
    "model",
    "id",
    "sequence",
    "battery",
    "pulses",
    "cumulative_kWh",
    "effect",
    "mode",
    NULL
};

r_device ikea_sparsnas = {
    .name          = "IKEA Sparsnäs Energy Meter",
    .modulation    = FSK_PULSE_PCM,
    .short_width   = 27,
    .long_width    = 27,
    .gap_limit     = 1000,
    .reset_limit   = 3000,
    .decode_fn     = &ikea_sparsnas_callback,
    .disabled      = 0,
    .fields        = output_fields
};
