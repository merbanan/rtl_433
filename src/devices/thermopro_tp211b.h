/** @file
    External helpers for ThermoPro TP211B proto-compiled decoder.
*/

#ifndef INCLUDE_THERMOPRO_TP211B_H_
#define INCLUDE_THERMOPRO_TP211B_H_

#include <stdbool.h>

#include "decoder.h"

static inline uint16_t thermopro_tp211b_xor_checksum(uint8_t const *b)
{
    static uint16_t const xor_table[] = {
            0xC881, 0xC441, 0xC221, 0xC111, 0xC089, 0xC045, 0xC023, 0xC010,
            0xC01F, 0xC00E, 0x6007, 0x9002, 0x4801, 0x8401, 0xE201, 0xD101,
            0xDE01, 0xCF01, 0xC781, 0xC3C1, 0xC1E1, 0xC0F1, 0xC079, 0xC03D,
            0xC029, 0xC015, 0xC00B, 0xC004, 0x6002, 0x3001, 0xB801, 0xFC01,
            0xE801, 0xD401, 0xCA01, 0xC501, 0xC281, 0xC141, 0xC0A1, 0xC051,
            0xC061, 0xC031, 0xC019, 0xC00D, 0xC007, 0xC002, 0x6001, 0x9001};
    uint16_t checksum = 0x411b;
    for (int n = 0; n < 6; n++) {
        for (int i = 0; i < 8; i++) {
            const int bit = (b[n] << (i + 1)) & 0x100;
            if (bit) {
                checksum ^= xor_table[(n * 8) + i];
            }
        }
    }
    return checksum;
}

/// Validate XOR-table checksum over the 8-byte frame.
static inline bool thermopro_tp211b_validate_checksum(
        int id, int flags, int temp_raw, int checksum)
{
    uint8_t b[8];
    b[0] = (uint8_t)(id >> 16);
    b[1] = (uint8_t)(id >> 8);
    b[2] = (uint8_t)(id & 0xff);
    b[3] = (uint8_t)((flags << 4) | (temp_raw >> 8));
    b[4] = (uint8_t)(temp_raw & 0xff);
    b[5] = 0xaa;
    b[6] = (uint8_t)(checksum >> 8);
    b[7] = (uint8_t)(checksum & 0xff);

    uint16_t calc = thermopro_tp211b_xor_checksum(b);
    return (uint16_t)checksum == calc;
}

#endif /* INCLUDE_THERMOPRO_TP211B_H_ */
