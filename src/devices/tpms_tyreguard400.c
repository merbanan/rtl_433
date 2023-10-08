/** @file
    TPMS TyreGuard 400 from Davies Craig.

    Copyright (C) 2022 R ALVERGNAT

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/**
TPMS TyreGuard 400 from Davies Craig.

- Type:            TPMS
- Freq:            434.1 MHz
- Modulation:      ASK -> OOK_MC_ZEROBIT (Manchester Code with fixed leading zero bit)
- Symbol duration: 100us (same for l or 0)
- Length:          22 bytes long

Packet layout:

    bytes : 1    2    3    4    5    6    7    8   9   10  11  12  13  14  15  16  17   18   19   20   21  22
    coded : S/P  S/P  S/P  S/P  S/P  S/P  S/P  ID  ID  ID  ID  ID  ID  ID  Pr  Pr  Temp Temp Flg  Flg  CRC CRC

- S/P   : preamble/sync "0xfd5fd5f" << always fixed
- ID    : 6 bytes long start with 0x6b????? ex 0x6b20d21
- Pr    : Last 2 bytes of pressure in psi ex : 0xe8 means XX232 psi (for XX see flags bytes)
- Temp  : Temperature in °C offset by +40 ex : 0x2f means (47-40)=+7°C
- Flg   : Flags bytes => should be read in binary format :
  - Bit 73 : Unknown ; maybe the 20th MSB pressure bit? The sensor is not capable to reach this so high pressure
  - Bit 74 : add 1024 psi (19th MSB pressure bit)
  - Bit 75 : add  512 psi (18th MSB pressure bit)
  - Bit 76 : add  256 psi (17th MSB pressure bit)
  - Bit 77 : Acknoldge pressure leaking 1=Ack 0=No_ack (nothing to report)
  - Bit 78 : Unknown
  - Bit 79 : Leaking pressure detected 1=Leak 0=No leak (nothing to report)
  - Bit 80 : Leaking pressure detected 1=Leak 0=No leak (nothing to report)
- CRC   : CRC poly 0x31 start value 0xdd final 0x00 from 1st bit 80th bits

To peer a new sensor to the unit, bit 79 and 80 has to be both to 1.

NOTE: In the datasheet, it is said that the sensor can report low batterie. During my tests/research i'm not able to see this behavior. I have fuzzed all bits nothing was reported to the reader.

Flex decoder:

    -X "n=TPMS,m=OOK_MC_ZEROBIT,s=100,l=100,r=500,preamble=fd5fd5f"

    decoder {
        name        = TPMS-TYREGUARD400,
        modulation  = OOK_MC_ZEROBIT,
        short       = 100,
        long        = 100,
        gap         = 0,
        reset       = 500,
        preamble    = fd5fd5f,
        get         = id:@0:{28},
        get         = pression:@57:{8},
        get         = temp:@65:{8},
        get         = flags:@73:{8},
        get         = add_psi:@74:{3}:[0:no 1:256 2:512 3:768 4:1024 5:1280 6:1536 7:1792],
        get         = AckLeaking:@77:{1}:[1: yes 0:no],
        get         = Leaking_detected:@79:{2}:[0:no 1:yes 2:yes],
        get         = CRC:@81:{8},
    }

*/

#include "decoder.h"

#define TPMS_TYREGUARD400_MESSAGE_BITLEN     88

static int tpms_tyreguard400_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[(TPMS_TYREGUARD400_MESSAGE_BITLEN + 7) / 8];

    // Extract the message
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, TPMS_TYREGUARD400_MESSAGE_BITLEN);

    //CRC poly 0x31 start value 0xdd final 0x00 from 1st bit to 80bits
    if (crc8(b, 11, 0x31, 0xdd) != 0) {
        decoder_log_bitrow(decoder, 2, __func__, b, TPMS_TYREGUARD400_MESSAGE_BITLEN, "CRC error");
        return DECODE_FAIL_MIC;
    }

    uint8_t flags = b[9];
    //int bat_low = flags & 0x1; // TBC ?!?
    int peering_request = flags & 0x3; // bytes = 0b00000011
    int ack_leaking = flags & 0x8; // byte = 0b00001000 :: NOTA ack_leaking = 1 means ack ;; ack_leaking=0 nothing to do

    // test if bits 1st or 2nd is set to 1 of 0b000000XX
    int leaking = flags & 0x3;

    //int add256  = (flags & 0x10) >> 4;  // bytes = 0b000X0000
    //int add512  = (flags & 0x20) >> 5;  // bytes = 0b00X00000
    //int add1024 = (flags & 0x40) >> 6; // bytes = 0b0X000000

    char id_str[8];
    snprintf(id_str, sizeof(id_str), "%07x", (uint32_t)(((b[3] & 0xf)<<24)) | (b[4]<<16) | (b[5]<<8) |  b[6]); // 28 bits ID
    char flags_str[3];
    snprintf(flags_str, sizeof(flags_str), "%02x", flags);

    //id = (b[4] << 8)
    int pressure_kpa = b[7] | ((flags & 0x70) << 4);
    int temp_c = b[8] - 40;

    /* clang-format off */
    data_t *data = data_make(
            "model",           "Model",                 DATA_STRING, "TyreGuard400",
            "type",            "Type",                  DATA_STRING, "TPMS",
            "id",              "ID",                    DATA_STRING, id_str,
//            "flags",           "Flags",                 DATA_STRING, flags_str,
            "pressure_kPa",    "Pressure",              DATA_FORMAT, "%.1f kPa",  DATA_DOUBLE, (double)pressure_kpa,
            "temperature_C",   "Temperature",           DATA_FORMAT, "%.0f C",    DATA_DOUBLE, (double)temp_c,
            "peering_request", "Peering req",           DATA_INT,    peering_request,
            "leaking",         "Leaking detected",      DATA_INT,    leaking,
            "ack_leaking",     "Ack leaking",           DATA_INT,    ack_leaking,
//            "add256",          "",                      DATA_INT,    add256,
//            "add512",          "",                      DATA_INT,    add512,
//            "add1024",          "",                     DATA_INT,    add1024,
//            "battery_ok",    "Batt OK",                 DATA_INT,    !bat_low,
            "mic",             "Integrity",             DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_tyreguard400_decode() */
static int tpms_tyreguard400_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    //uint8_t const tyreguard_frame_sync[] = {0xf, 0xd5, 0xfd, 0x5f}
    uint8_t const tyreguard_frame_sync[] = {0xfd, 0x5f, 0xd5, 0xf0}; // needs to shift sync to align bytes 28x bits useful

    int ret    = 0;
    int events = 0;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        if (bitbuffer->bits_per_row[row] < TPMS_TYREGUARD400_MESSAGE_BITLEN) {
            // bail out of this "too short" row early
            if (decoder->verbose >= 2) {
                // Output the bad row, only for message level debug / deciphering.
                decoder_logf_bitrow(decoder, 2, __func__, bitbuffer->bb[row], bitbuffer->bits_per_row[row],
                        "Bad message in row %d need %d bits got %d",
                        row, TPMS_TYREGUARD400_MESSAGE_BITLEN, bitbuffer->bits_per_row[row]);
            }
            continue; // DECODE_ABORT_LENGTH
        }

        unsigned bitpos = 0;

        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos, tyreguard_frame_sync, 28)) + TPMS_TYREGUARD400_MESSAGE_BITLEN <=
                bitbuffer->bits_per_row[row]) {

            if (decoder->verbose >= 2) {
                decoder_logf_bitrow(decoder, 2, __func__, bitbuffer->bb[row], bitbuffer->bits_per_row[row],
                        "Find bitpos with preamble row %d at %u", row, bitpos);
            }

            ret = tpms_tyreguard400_decode(decoder, bitbuffer, row, bitpos);
            if (ret > 0)
                events += ret;

            bitpos += TPMS_TYREGUARD400_MESSAGE_BITLEN;
        }
    }
    // (Only) for future regression tests.
    if ((decoder->verbose >= 3) & (events == 0)) {
        decoder_logf(decoder, 3, __func__, "Bad transmission");
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
//        "flags",
        "pressure_kPa",
        "temperature_C",
        "peering_request",
        "leaking",
        "ack_leaking",
//        "add256",
//        "add512",
//        "add1024",
//        "battery_ok",
        "mic",
        NULL,
};

r_device const tpms_tyreguard400 = {
        .name        = "TyreGuard 400 TPMS",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 100,
        .long_width  = 100,
        .gap_limit   = 0,
        .reset_limit = 500,
        .decode_fn   = &tpms_tyreguard400_callback,
        .fields      = output_fields,
};
