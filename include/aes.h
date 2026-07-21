/** @file
    Minimal AES-128 encryption and CMAC helpers.

    Copyright (C) 2026 Benjamin Larsson <banan@ludd.ltu.se>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef INCLUDE_AES_H_
#define INCLUDE_AES_H_

#include <stddef.h>
#include <stdint.h>

void aes128_encrypt_block(uint8_t const key[16], uint8_t const input[16],
        uint8_t output[16]);

void aes128_cmac(uint8_t const key[16], uint8_t const *message,
        size_t message_len, uint8_t output[16]);

#endif /* INCLUDE_AES_H_ */
