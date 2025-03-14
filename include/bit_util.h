/** @file
    Various utility functions for use by device drivers.

    Copyright (C) 2015 Tommy Vestermark

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef INCLUDE_BIT_UTIL_H_
#define INCLUDE_BIT_UTIL_H_

#include <stdint.h>

/// Reverse (reflect) the bits in an 32 bit byte.
///
/// @param x input byte
/// @return bit reversed byte
uint32_t reverse32(uint32_t x);


/// Reverse (reflect) the bits in an 8 bit byte.
///
/// @param x input byte
/// @return bit reversed byte
uint8_t reverse8(uint8_t x);

/// Reflect (reverse LSB to MSB) each byte of a number of bytes.
///
/// @param message bytes of message data
/// @param num_bytes number of bytes to reflect
void reflect_bytes(uint8_t message[], unsigned num_bytes);

/// Reflect (reverse LSB to MSB) each nibble in an 8 bit byte, preserves nibble order.
///
/// @param x input byte
/// @return reflected nibbles
uint8_t reflect4(uint8_t x);

/// Reflect (reverse LSB to MSB) each nibble in a number of bytes.
///
/// @param message bytes of nibble message data
/// @param num_bytes number of bytes to reflect
void reflect_nibbles(uint8_t message[], unsigned num_bytes);

/// Unstuff nibbles with 1-bit separator (4B1S) to bytes, returns number of successfully unstuffed nibbles.
///
/// @param message bytes of message data
/// @param offset_bits start offset of message in bits
/// @param num_bits message length in bits
/// @param dst target buffer for extracted nibbles, at least num_bits/5 size
/// @return number of successfully unstuffed nibbles.
unsigned extract_nibbles_4b1s(uint8_t const *message, unsigned offset_bits, unsigned num_bits, uint8_t *dst);

/// UART "8n1" (10-to-8) decoder with 1 start bit (0), no parity, 1 stop bit (1), LSB-first bit-order.
///
/// @param message bytes of message data
/// @param offset_bits start offset of message in bits
/// @param num_bits message length in bits
/// @param dst target buffer for extracted bytes, at least num_bits/10 size
/// @return number of successful decoded bytes
unsigned extract_bytes_uart(uint8_t const *message, unsigned offset_bits, unsigned num_bits, uint8_t *dst);

/// UART "8o1" (11-to-8) decoder with 1 start bit (1), odd parity, 1 stop bit (0), MSB-first bit-order.
///
/// @param message bytes of message data
/// @param offset_bits start offset of message in bits
/// @param num_bits message length in bits
/// @param dst target buffer for extracted bytes, at least num_bits/11 size
/// @return number of successful decoded bytes
unsigned extract_bytes_uart_parity(uint8_t const *message, unsigned offset_bits, unsigned num_bits, uint8_t *dst);

/// Decode symbols to bits.
///
/// @param message bytes of message data
/// @param offset_bits start offset of message in bits
/// @param num_bits message length in bits
/// @param zero symbol for zero bit, bits MSB aligned, count in LSB
/// @param one symbol for one bit, bits MSB aligned, count in LSB
/// @param sync symbol for sync bit, ignored at start, terminates at end
/// @param dst target buffer for extracted bits, at least num_bits/symbol_x_len size
/// @return number of successful decoded bits
unsigned extract_bits_symbols(uint8_t const *message, unsigned offset_bits, unsigned num_bits, uint32_t zero, uint32_t one, uint32_t sync, uint8_t *dst);

/// CRC-4.
///
/// @param message array of bytes to check
/// @param nBytes number of bytes in message
/// @param polynomial CRC polynomial
/// @param init starting crc value
/// @return CRC value
uint8_t crc4(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// CRC-7.
///
/// @param message array of bytes to check
/// @param nBytes number of bytes in message
/// @param polynomial CRC polynomial
/// @param init starting crc value
/// @return CRC value
uint8_t crc7(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// Generic Cyclic Redundancy Check CRC-8.
///
/// Example polynomial: 0x31 = x8 + x5 + x4 + 1 (x8 is implicit)
/// Example polynomial: 0x80 = x8 + x7 (a normal bit-by-bit parity XOR)
///
/// @param message array of bytes to check
/// @param nBytes number of bytes in message
/// @param polynomial byte is from x^7 to x^0 (x^8 is implicitly one)
/// @param init starting crc value
/// @return CRC value
uint8_t crc8(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// "Little-endian" Cyclic Redundancy Check CRC-8 LE
/// Input and output are reflected, i.e. least significant bit is shifted in first.
///
/// @param message array of bytes to check
/// @param nBytes number of bytes in message
/// @param polynomial CRC polynomial
/// @param init starting crc value
/// @return CRC value
uint8_t crc8le(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// CRC-16 LSB.
/// Input and output are reflected, i.e. least significant bit is shifted in first.
/// Note that poly and init already need to be reflected.
///
/// @param message array of bytes to check
/// @param nBytes number of bytes in message
/// @param polynomial CRC polynomial
/// @param init starting crc value
/// @return CRC value
uint16_t crc16lsb(uint8_t const message[], unsigned nBytes, uint16_t polynomial, uint16_t init);

/// CRC-16.
///
/// @param message array of bytes to check
/// @param nBytes number of bytes in message
/// @param polynomial CRC polynomial
/// @param init starting crc value
/// @return CRC value
uint16_t crc16(uint8_t const message[], unsigned nBytes, uint16_t polynomial, uint16_t init);

/// Digest-8 by "LFSR-based Toeplitz hash", bits MSB to LSB.
///
/// @param message bytes of message data
/// @param bytes number of bytes to digest
/// @param gen key stream generator, needs to includes the MSB for ROR if the LFSR is rolling
/// @param key initial key
/// @return digest value
uint8_t lfsr_digest8(uint8_t const message[], unsigned bytes, uint8_t gen, uint8_t key);

/// Digest-8 by "LFSR-based Toeplitz hash", byte reversed, bits MSB to LSB.
///
/// @param message bytes of message data, read in reverse
/// @param bytes number of bytes to digest
/// @param gen key stream generator, needs to includes the MSB for ROR if the LFSR is rolling
/// @param key initial key
/// @return digest value
uint8_t lfsr_digest8_reverse(uint8_t const message[], int bytes, uint8_t gen, uint8_t key);

/// Digest-8 by "LFSR-based Toeplitz hash", byte reversed, bit reflect (LSB to MSB).
///
/// @param message bytes of message data, read in reverse
/// @param bytes number of bytes to digest
/// @param gen key stream generator, needs to includes the LSB for ROL if the LFSR is rolling
/// @param key initial key
/// @return digest value
uint8_t lfsr_digest8_reflect(uint8_t const message[], int bytes, uint8_t gen, uint8_t key);

/// Digest-16 by "LFSR-based Toeplitz hash".
///
/// @param message bytes of message data
/// @param bytes number of bytes to digest
/// @param gen key stream generator, needs to includes the MSB if the LFSR is rolling
/// @param key initial key
/// @return digest value
uint16_t lfsr_digest16(uint8_t const message[], unsigned bytes, uint16_t gen, uint16_t key);

/// Apply CCITT data whitening to a buffer.
///
/// The CCITT data whitening process is built around a 9-bit Linear Feedback Shift Register (LFSR).
/// The LFSR polynomial is the same polynomial as for IBM data whitening (x9 + x5 + 1).
/// The initial value of the data whitening key is set to all ones, 0x1FF.
/// s.a. https://www.nxp.com/docs/en/application-note/AN5070.pdf s.5.2
///
/// @param buffer bytes of message data
/// @param buffer_size number of bytes to process
void ccitt_whitening(uint8_t *buffer, unsigned buffer_size);

/// Compute bit parity of a single byte (8 bits).
///
/// @param byte single byte to check
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

/// Compute Addition of a number of nibbles (byte wise).
///
/// @param message bytes (of two nibbles) of message data
/// @param num_bytes number of bytes to sum
/// @return summation value
int add_nibbles(uint8_t const message[], unsigned num_bytes);

#endif /* INCLUDE_BIT_UTIL_H_ */
