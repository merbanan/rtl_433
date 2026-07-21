/** @file
    Minimal AES-128 encryption and AES-CMAC implementation.

    The transformations and key schedule follow FIPS 197. CMAC follows
    NIST SP 800-38B and RFC 4493. This module is intended for protocol
    decoding and is not hardened against timing or cache side channels.
*/

#include "aes.h"

#include <string.h>

static uint8_t const sbox[256] = {
        0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
        0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
        0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
        0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
        0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
        0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
        0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
        0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
        0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
        0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
        0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
        0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
        0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
        0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
        0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
        0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
};

static uint8_t xtime(uint8_t value)
{
    return (uint8_t)((value << 1) ^ ((value >> 7) * 0x1b));
}

static void expand_key(uint8_t const key[16], uint8_t round_keys[176])
{
    static uint8_t const rcon[10] = {
            0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
    };
    memcpy(round_keys, key, 16);
    unsigned generated = 16;
    unsigned round = 0;
    uint8_t word[4];
    while (generated < 176) {
        memcpy(word, &round_keys[generated - 4], sizeof(word));
        if (generated % 16 == 0) {
            uint8_t const first = word[0];
            word[0] = sbox[word[1]] ^ rcon[round++];
            word[1] = sbox[word[2]];
            word[2] = sbox[word[3]];
            word[3] = sbox[first];
        }
        for (unsigned i = 0; i < 4; ++i) {
            round_keys[generated] = round_keys[generated - 16] ^ word[i];
            generated += 1;
        }
    }
}

static void add_round_key(uint8_t state[16], uint8_t const round_key[16])
{
    for (unsigned i = 0; i < 16; ++i) {
        state[i] ^= round_key[i];
    }
}

static void sub_bytes(uint8_t state[16])
{
    for (unsigned i = 0; i < 16; ++i) {
        state[i] = sbox[state[i]];
    }
}

static void shift_rows(uint8_t state[16])
{
    uint8_t shifted[16];
    shifted[0] = state[0];
    shifted[1] = state[5];
    shifted[2] = state[10];
    shifted[3] = state[15];
    shifted[4] = state[4];
    shifted[5] = state[9];
    shifted[6] = state[14];
    shifted[7] = state[3];
    shifted[8] = state[8];
    shifted[9] = state[13];
    shifted[10] = state[2];
    shifted[11] = state[7];
    shifted[12] = state[12];
    shifted[13] = state[1];
    shifted[14] = state[6];
    shifted[15] = state[11];
    memcpy(state, shifted, sizeof(shifted));
}

static void mix_columns(uint8_t state[16])
{
    for (unsigned column = 0; column < 4; ++column) {
        uint8_t *s = &state[4 * column];
        uint8_t const all = s[0] ^ s[1] ^ s[2] ^ s[3];
        uint8_t const first = s[0];
        s[0] ^= all ^ xtime(s[0] ^ s[1]);
        s[1] ^= all ^ xtime(s[1] ^ s[2]);
        s[2] ^= all ^ xtime(s[2] ^ s[3]);
        s[3] ^= all ^ xtime(s[3] ^ first);
    }
}

void aes128_encrypt_block(uint8_t const key[16], uint8_t const input[16],
        uint8_t output[16])
{
    uint8_t round_keys[176];
    uint8_t state[16];
    expand_key(key, round_keys);
    memcpy(state, input, sizeof(state));
    add_round_key(state, round_keys);
    for (unsigned round = 1; round < 10; ++round) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, &round_keys[16 * round]);
    }
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, &round_keys[160]);
    memcpy(output, state, sizeof(state));
}

static void left_shift_block(uint8_t output[16], uint8_t const input[16])
{
    uint8_t carry = 0;
    for (unsigned i = 16; i > 0; --i) {
        uint8_t const next = input[i - 1] >> 7;
        output[i - 1] = (uint8_t)(input[i - 1] << 1 | carry);
        carry = next;
    }
}

void aes128_cmac(uint8_t const key[16], uint8_t const *message,
        size_t message_len, uint8_t output[16])
{
    uint8_t zero[16] = {0};
    uint8_t subkey1[16];
    uint8_t subkey2[16];
    aes128_encrypt_block(key, zero, subkey1);
    uint8_t const msb1 = subkey1[0] >> 7;
    left_shift_block(subkey1, subkey1);
    subkey1[15] ^= msb1 * 0x87;
    uint8_t const msb2 = subkey1[0] >> 7;
    left_shift_block(subkey2, subkey1);
    subkey2[15] ^= msb2 * 0x87;

    size_t const block_count = message_len ? (message_len + 15) / 16 : 1;
    int const complete = message_len && message_len % 16 == 0;
    uint8_t last[16] = {0};
    size_t const last_len = complete ? 16 : message_len % 16;
    if (last_len) {
        memcpy(last, message + 16 * (block_count - 1), last_len);
    }
    if (!complete) {
        last[last_len] = 0x80;
    }
    uint8_t const *subkey = complete ? subkey1 : subkey2;
    for (unsigned i = 0; i < 16; ++i) {
        last[i] ^= subkey[i];
    }

    uint8_t chain[16] = {0};
    uint8_t block[16];
    for (size_t n = 0; n + 1 < block_count; ++n) {
        for (unsigned i = 0; i < 16; ++i) {
            block[i] = chain[i] ^ message[16 * n + i];
        }
        aes128_encrypt_block(key, block, chain);
    }
    for (unsigned i = 0; i < 16; ++i) {
        block[i] = chain[i] ^ last[i];
    }
    aes128_encrypt_block(key, block, output);
}

#ifdef _TEST
#include <stdio.h>

int main(void)
{
    uint8_t const key[16] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    uint8_t const plaintext[16] = {
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    uint8_t const ciphertext[16] = {
            0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
            0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a,
    };
    uint8_t output[16];
    aes128_encrypt_block(key, plaintext, output);
    if (memcmp(output, ciphertext, sizeof(output)) != 0) {
        fprintf(stderr, "aes: FIPS 197 block vector failed\n");
        return 1;
    }

    uint8_t const cmac_key[16] = {
            0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
            0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    };
    uint8_t const cmac_empty[16] = {
            0xbb, 0x1d, 0x69, 0x29, 0xe9, 0x59, 0x37, 0x28,
            0x7f, 0xa3, 0x7d, 0x12, 0x9b, 0x75, 0x67, 0x46,
    };
    aes128_cmac(cmac_key, NULL, 0, output);
    if (memcmp(output, cmac_empty, sizeof(output)) != 0) {
        fprintf(stderr, "aes: RFC 4493 empty CMAC vector failed\n");
        return 1;
    }
    fprintf(stderr, "aes: tests passed\n");
    return 0;
}
#endif
