/** @file
    Various utility functions for use by device drivers.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_UTIL_H_
#define INCLUDE_UTIL_H_

#include <stdint.h>

// Helper macros, collides with MSVC's stdlib.h unless NOMINMAX is used
#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

/// Reverse (reflect) the bits in an 8 bit byte.
///
/// @param x: input byte
/// @return bit reversed byte
uint8_t reverse8(uint8_t x);

/// Reflect (reverse LSB to MSB) each byte of a number of bytes.
///
/// @param message bytes of message data
/// @param num_bytes number of bytes to reflect
void reflect_bytes(uint8_t message[], unsigned num_bytes);

/// CRC-4.
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: CRC polynomial
/// @param init: starting crc value
/// @return CRC value
uint8_t crc4(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// CRC-7.
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: CRC polynomial
/// @param init: starting crc value
/// @return CRC value
uint8_t crc7(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// Generic Cyclic Redundancy Check CRC-8.
///
/// Example polynomial: 0x31 = x8 + x5 + x4 + 1 (x8 is implicit)
/// Example polynomial: 0x80 = x8 + x7 (a normal bit-by-bit parity XOR)
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: byte is from x^7 to x^0 (x^8 is implicitly one)
/// @param init: starting crc value
/// @return CRC value
uint8_t crc8(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// "Little-endian" Cyclic Redundancy Check CRC-8 LE
/// Input and output are reflected, i.e. least significant bit is shifted in first.
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: CRC polynomial
/// @param init: starting crc value
/// @return CRC value
uint8_t crc8le(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// CRC-16 LSB.
/// Input and output are reflected, i.e. least significant bit is shifted in first.
/// Note that poly and init already need to be reflected.
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: CRC polynomial
/// @param init: starting crc value
/// @return CRC value
uint16_t crc16lsb(uint8_t const message[], unsigned nBytes, uint16_t polynomial, uint16_t init);

/// CRC-16.
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: CRC polynomial
/// @param init: starting crc value
/// @return CRC value
uint16_t crc16(uint8_t const message[], unsigned nBytes, uint16_t polynomial, uint16_t init);

/// Digest-8 by "LFSR-based Toeplitz hash".
///
/// @param message bytes of message data
/// @param bytes number of bytes to digest
/// @param gen key stream generator, needs to includes the MSB if the LFSR is rolling
/// @param key initial key
/// @return digest value
uint8_t lfsr_digest8(uint8_t const message[], unsigned bytes, uint8_t gen, uint8_t key);

/// Digest-16 by "LFSR-based Toeplitz hash".
///
/// @param data up to 32 bits data, LSB aligned
/// @param bits number of bits to digest
/// @param gen key stream generator, needs to includes the MSB if the LFSR is rolling
/// @param key initial key
/// @return digest value
uint16_t lfsr_digest16(uint32_t data, int bits, uint16_t gen, uint16_t key);

/// Compute bit parity of a single byte (8 bits).
///
/// @param byte: single byte to check
/// @return 1 odd parity, 0 even parity
int parity8(uint8_t byte);

/// Compute bit parity of a number of bytes.
///
/// @param message bytes of message data
/// @param num_bytes number of bytes to sum
/// @return 1 odd parity, 0 even parity
int parity_bytes(uint8_t const message[], unsigned num_bytes);

/// Compute XOR (byte-wide parity) of a number of bytes.
///
/// @param message bytes of message data
/// @param num_bytes number of bytes to sum
/// @return summation value, per bit-position 1 odd parity, 0 even parity
uint8_t xor_bytes(uint8_t const message[], unsigned num_bytes);

/// Compute Addition of a number of bytes.
///
/// @param message bytes of message data
/// @param num_bytes number of bytes to sum
/// @return summation value
int add_bytes(uint8_t const message[], unsigned num_bytes);

#endif /* INCLUDE_UTIL_H_ */
