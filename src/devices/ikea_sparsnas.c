/*
IKEA Sparsnäs Energy Meter Monitor decoder

The IKEA Sparsnäs consists of a display unit, and a sender unit. The display unit 
displays and stores the values sent by the sender unit. It is not needed for this
decoder. The sender unit is placed by the energy meter. The sender unit has an
IR photo sensor which is placed over the energy meter impulse diode. The sender
also has an external antenna, which should be placed where it can provide non-
interfered transmissions.

The energy meter sends a fixed number of pulses per kWh. This is different per
unit, but usual values are 500, 1000 and 2000. This is usually indicated like

1000 imp/kWh

on the front of the meter. This value goes into ikea_sparsnas_pulses_per_kwh 
in this file. The sender also has a unique ID which is used in the encryption
key, hence it is needed here to decrypt the data. The sender ID is on a sticker
in the battery compartment. There are three groups of three digits there. The
last six digits are your sender ID. Eg "400 617 633" gives you the sender id 
617633. This number goes into IKEA_SPARSNAS_SENSOR_ID in this file.


The data is sent using CPFSK modulation. It requires PD_MIN_PULSE_SAMPLES in 
pulse_detect.h to be lowered to 5 to be able to demodulate at 250kS/s. The
preamble is optimally 4 bytes of 0XAA. Then the sync word 0xD201. Here only 
the last 2 bytes of the 0xAA preamble is checked, as the first ones seems
to be corrupted quite often. There are plenty of integrety checks made on 
the demodulated package which makes this compromise OK.

Packet structure according to: https://github.com/strigeus/sparsnas_decoder 
(with some changes by myself)

0:  uint8_t length;        // Always 0x11
1:  uint8_t sender_id_lo;  // Lowest byte of sender ID
2:  uint8_t unknown;       // Not sure
3:  uint8_t major_version; // Always 0x07 - the major version number of the sender.
4:  uint8_t minor_version; // Always 0x0E - the minor version number of the sender.
5:  uint32_t sender_id;    // ID of sender
9:  uint16_t sequence;     // Sequence number of current packet
11: uint16_t effect;       // Current effect usage
13: uint32_t pulses;       // Total number of pulses
17: uint8_t battery;       // Battery level, 0-100%
18: uint16_t CRC;          // 16 bit CRC of bytes 0-17

Example packet: 0x11a15f070ea2dfefe6d5fdd20547e6340ae7be61


The packet's integrety can be checked with the 16b CRC at the end of the packet. 
There are also several other ways to check the integrety of the package. 
 - (preamble)
 - CRC
 - The decrypted sensor ID
 - the constant bytes at 0, 3 and 4

The decryption, CRC is calculation, value extraction and interpretation is 
taken from https://github.com/strigeus/sparsnas_decoder and adapted to 
this application. Many thanks to strigeus!

Most other things are from https://github.com/kodarn/Sparsnas which is an
amazing repository of the IKEA Sparsnäs. Everything is studied with greay
detail. Many thanks to kodarn!

*/


#include "decoder.h"
#define IKEA_SPARSNAS_MESSAGE_BITLEN 160    // 20 bytes incl 8 bit length, 8 bit address, 128 bits data, and 16 bits of CRC. Excluding preamble and sync word
#define IKEA_SPARSNAS_MESSAGE_BYTELEN    ((IKEA_SPARSNAS_MESSAGE_BITLEN + 7) / 8)
#define IKEA_SPARSNAS_MESSAGE_BITLEN_MAX 260 // Just for early sanity checks

#define IKEA_SPARSNAS_PREAMBLE_BITLEN 32
static const uint8_t preamble_pattern[4] = {0xAA, 0xAA, 0xD2, 0x01};

#define IKEA_SPARSNAS_CRC_INIT 0xffff
#define IKEA_SPARSNAS_CRC_POLY 0x8005

#define IKEA_SPARSNAS_ID_KEY_SUB 0x5D38E8CB

static uint16_t ikea_sparsnas_pulses_per_kwh = 1000;
static uint32_t ikea_sparsnas_sensor_id = 0;

static uint32_t ikea_sparsnas_brute_force_encryption(uint8_t buffer[18])
{
    const uint8_t b5 = buffer[5 + 0];
    const uint8_t b6 = buffer[5 + 1];
    const uint8_t b7 = buffer[5 + 2];
    const uint8_t b8 = buffer[5 + 3];
    const uint8_t battery_enc = buffer[17];

    uint8_t d0, d1, d2;
    const uint8_t d3 = b8 ^ 0x47;

    uint32_t dec_sensor_id, key_sensor_id;
    uint8_t battery_dec, k0, k1, k2, k4;

    for (k0=0;k0<0xFF;++k0) {
        d0 = b5 ^ k0;
        if (d0 > 0x0F) { //will result in sensor_id > 999999
            continue; // DECODE_FAIL_SANITY
        }
        for (k1=0;k1<0xFF;++k1) {
            d1 = b6 ^ k1;

            for (k2=0;k2<0xFF;++k2) {

                d2 = b7 ^ k2;
                battery_dec = battery_enc ^ k2;
                dec_sensor_id = (unsigned)d0 << 24 | d1 << 16 | d2 << 8 | d3;

                if (dec_sensor_id > 999999) { //sensor id is at most 6 digits
                    continue; // DECODE_FAIL_SANITY
                }

                for (k4=0;k4<0xFF;++k4) {
                    key_sensor_id  = ((unsigned)k0 << 24 | k4 << 16 | k2 << 8 | k1) + IKEA_SPARSNAS_ID_KEY_SUB;

                    if ((dec_sensor_id == key_sensor_id) && (battery_dec <= 100)) {
                        return dec_sensor_id;
                    }
                }
            }
        }
    }
   return 0;
}

static int ikea_sparsnas_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{

    if ((bitbuffer->bits_per_row[0] < IKEA_SPARSNAS_MESSAGE_BITLEN) || (bitbuffer->bits_per_row[0] > IKEA_SPARSNAS_MESSAGE_BITLEN_MAX)) {
        if (decoder->verbose > 1) {
            decoder_output_bitbufferf(decoder, bitbuffer, "IKEA Sparsnäs");
            fprintf(stderr, "IKEA Sparsnäs: Too short or too long packet received. Expected %d, received %d\n", IKEA_SPARSNAS_MESSAGE_BITLEN, bitbuffer->bits_per_row[0]);
        }
        return DECODE_ABORT_LENGTH;
    }

    // Look for preamble
    uint16_t bitpos = bitbuffer_search(bitbuffer, 0, 0, (const uint8_t *)&preamble_pattern, IKEA_SPARSNAS_PREAMBLE_BITLEN);

    if ((bitbuffer->bits_per_row[0] == bitpos) || (bitpos + IKEA_SPARSNAS_MESSAGE_BITLEN > bitbuffer->bits_per_row[0])) {
        if (decoder->verbose > 1) {
            decoder_output_bitbufferf(decoder, bitbuffer, "IKEA Sparsnäs");
            fprintf(stderr, "IKEA Sparsnäs: malformed package, preamble not found. (Expected 0xAAAAD201)\n");
        }
        return DECODE_ABORT_EARLY;
    }

    // extract message, discarding preamble
    uint8_t buffer[IKEA_SPARSNAS_MESSAGE_BYTELEN];
    bitbuffer_extract_bytes(bitbuffer, 0, bitpos + IKEA_SPARSNAS_PREAMBLE_BITLEN, buffer, IKEA_SPARSNAS_MESSAGE_BITLEN);

    if (decoder->verbose > 1) {
        decoder_output_bitbufferf(decoder, bitbuffer, "IKEA Sparsnäs");
        decoder_output_bitrowf(decoder, buffer, IKEA_SPARSNAS_MESSAGE_BITLEN, "Encrypted message");
    }
    // CRC check
    uint16_t crc_calculated = crc16(buffer, IKEA_SPARSNAS_MESSAGE_BYTELEN - 2, IKEA_SPARSNAS_CRC_POLY, IKEA_SPARSNAS_CRC_INIT);
    uint16_t crc_received = buffer[18] << 8 | buffer[19];

    if (crc_received != crc_calculated) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "IKEA Sparsnäs: CRC check failed (0x%X != 0x%X)\n", crc_calculated, crc_received);
        }
        return DECODE_FAIL_MIC;
    }

    //Decryption
    if (!ikea_sparsnas_sensor_id) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "IKEA Sparsnäs: No sensor ID configured. Brute forcing encryption.\n");
        }
        ikea_sparsnas_sensor_id = ikea_sparsnas_brute_force_encryption(buffer);
        if (decoder->verbose > 1) {
            if (ikea_sparsnas_sensor_id) {
                fprintf(stderr, "IKEA Sparsnäs: Found valid sensor ID %06d. If reported values does not make sense, this might be incorrect.\n", ikea_sparsnas_sensor_id);
            } else {
                fprintf(stderr, "IKEA Sparsnäs: No valid sensor ID found.\n");
            }
        }
    }

    uint8_t decrypted[18];

    uint8_t key[5];
    const uint32_t sensor_id_sub = ikea_sparsnas_sensor_id - IKEA_SPARSNAS_ID_KEY_SUB;

    key[0] = (uint8_t)(sensor_id_sub >> 24);
    key[1] = (uint8_t)(sensor_id_sub);
    key[2] = (uint8_t)(sensor_id_sub >> 8);
    key[3] = 0x47;
    key[4] = (uint8_t)(sensor_id_sub >> 16);

    for (size_t i = 0; i < 5; i++)
        decrypted[i] = buffer[i];

    for (size_t i = 0; i < 13; i++)
        decrypted[5 + i] = buffer[5 + i] ^ key[i % 5];

    // Additional integrity checks
    uint32_t rcv_sensor_id = (unsigned)decrypted[5] << 24 | decrypted[6] << 16 | decrypted[7] << 8 | decrypted[8];

    if (decoder->verbose > 1) {
        fprintf(stderr, "IKEA Sparsnäs: CRC OK (%X == %X)\n", crc_calculated, crc_received);
        fprintf(stderr, "IKEA Sparsnäs: Encryption key: 0x%X%X%X%X%X\n", key[0], key[1], key[2], key[3], key[4]);
        decoder_output_bitrowf(decoder, decrypted, 18 * 8, "Decrypted");
        fprintf(stderr, "IKEA Sparsnäs: Received sensor id: %d\n", rcv_sensor_id);
    }

    if (rcv_sensor_id != ikea_sparsnas_sensor_id) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "IKEA Sparsnäs: Malformed package, or wrong sensor id. Received sensor id (%d) not the same as sender (%d)\n", rcv_sensor_id, ikea_sparsnas_sensor_id);
        }
    }

    if ((!ikea_sparsnas_sensor_id) || (rcv_sensor_id != ikea_sparsnas_sensor_id)) {

        data_t *data;
        data = data_make(
            "model",         "Model",               DATA_STRING, "IKEA Sparsnäs Energy Meter Monitor [Encrypted]",
            "id",            "Sensor ID",           DATA_INT, ikea_sparsnas_sensor_id,
            "mic",           "Integrity",           DATA_STRING,    "CRC",
            NULL
        );
        decoder_output_data(decoder, data);
        return 1;
    }

    if (decrypted[0] != 0x11) {
        decoder_output_bitrowf(decoder, decrypted + 5, 13 * 8,  "Message malformed");
        if (decoder->verbose > 1) {
            fprintf(stderr, "IKEA Sparsnäs: Message malformed (byte0=%X expected %X)\n", decrypted[0], 0x11);
        }
        return DECODE_FAIL_SANITY;
    }
    if (decrypted[3] != 0x07) {
        decoder_output_bitrowf(decoder, decrypted + 5, 13 * 8,  "Message malformed");
        if (decoder->verbose > 1) {
            fprintf(stderr, "IKEA Sparsnäs: Message malformed (byte3=%X expected %X)\n", decrypted[0], 0x07);
        }
        return DECODE_FAIL_SANITY;
    }

    //Value extraction and interpretation
    uint16_t sequence_number = (decrypted[9] << 8 | decrypted[10]);
    uint16_t effect = (decrypted[11] <<  8 | decrypted[12]);
    uint32_t pulses = ((unsigned)decrypted[13] << 24 | decrypted[14] << 16 | decrypted[15] << 8 | decrypted[16]);
    uint8_t battery =  decrypted[17];
    float watt = effect * 24.;
    uint8_t mode = decrypted[4]^0x0f;

    if (mode == 1) {     //Note that mode cycles between 0-3 when you first put in the batteries in
      watt = ((3600000.0 / ikea_sparsnas_pulses_per_kwh) * 1024.0) / effect;
    } else if (mode == 0 ) { // special mode for low power usage
      watt = effect * 0.24 / ikea_sparsnas_pulses_per_kwh;
    }
    float cumulative_kWh = ((float)pulses) / ((float)ikea_sparsnas_pulses_per_kwh);

    data_t *data;
    /* clang-format off */
    data = data_make(
            "model",         "Model",               DATA_STRING, "Ikea-Sparsnas",
            "id",            "Sensor ID",           DATA_INT, rcv_sensor_id,
            "sequence",      "Sequence Number",     DATA_INT, sequence_number,
            "battery",       "Battery",             DATA_FORMAT, "%d%%", DATA_INT, battery,
            "pulses_per_kWh", "Pulses per kWh",     DATA_INT, ikea_sparsnas_pulses_per_kwh,
            "cumulative_kWh", "Cumulative kWh",     DATA_FORMAT, "%7.3fkWh", DATA_DOUBLE,  cumulative_kWh,
            "effect",        "Effect",              DATA_FORMAT, "%dW", DATA_INT,  effect,
            "pulses",        "Pulses",              DATA_INT,  pulses,
            "mode",          "Mode",                DATA_INT, mode,
            "mic",           "Integrity",           DATA_STRING,    "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "sequence",
    "battery",
    "pulses_per_kwh",
    "cumulative_kWh",
    "effect",
    "pulses",
    "mode",
    "mic",
    NULL,
};

r_device ikea_sparsnas = {
    .name          = "IKEA Sparsnäs Energy Meter Monitor",
    .modulation    = FSK_PULSE_PCM,
    .short_width   = 27,
    .long_width    = 27,
    .gap_limit     = 1000,
    .reset_limit   = 3000,
    .decode_fn     = &ikea_sparsnas_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
