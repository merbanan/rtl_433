/* thermopro_tp211b.h – hand-written helpers included by the generated decoder. */
#pragma once

static uint16_t tp211b_checksum(uint8_t const *b)
{
    static uint16_t const xor_table[] = {
            0xC881, 0xC441, 0xC221, 0xC111, 0xC089, 0xC045, 0xC023, 0xC010,
            0xC01F, 0xC00E, 0x6007, 0x9002, 0x4801, 0x8401, 0xE201, 0xD101,
            0xDE01, 0xCF01, 0xC781, 0xC3C1, 0xC1E1, 0xC0F1, 0xC079, 0xC03D,
            0xC029, 0xC015, 0xC00B, 0xC004, 0x6002, 0x3001, 0xB801, 0xFC01,
            0xE801, 0xD401, 0xCA01, 0xC501, 0xC281, 0xC141, 0xC0A1, 0xC051,
            0xC061, 0xC031, 0xC019, 0xC00D, 0xC007, 0xC002, 0x6001, 0x9001,
    };
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

static int thermopro_tp211b_validate(uint8_t *b, int checksum)
{
    return ((int)tp211b_checksum(b) == checksum) ? 0 : DECODE_FAIL_MIC;
}
