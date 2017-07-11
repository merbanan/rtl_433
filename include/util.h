/**
 * Various utility functions for use by device drivers
 * 
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef INCLUDE_UTIL_H_
#define INCLUDE_UTIL_H_

#include <stdint.h>
#include <time.h>

// Helper macros
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

/// Reverse the bits in an 8 bit byte
/// @param x: input byte
/// @return bit reversed byte
uint8_t reverse8(uint8_t x);

/// CRC-7
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: CRC polynomial
/// @param init: starting crc value
/// @return CRC value
uint8_t crc7(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// Generic Cyclic Redundancy Check CRC-8
///
/// Example polynomial: 0x31 = x8 + x5 + x4 + 1	(x8 is implicit)
/// Example polynomial: 0x80 = x8 + x7			(a normal bit-by-bit parity XOR)
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: byte is from x^7 to x^0 (x^8 is implicitly one)
/// @param init: starting crc value
/// @return CRC value
uint8_t crc8(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// "Little-endian" Cyclic Redundancy Check CRC-8 LE
///
/// Based on code generated from PyCyc, (pycrc.org)
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: CRC polynomial
/// @param init: starting crc value
/// @return CRC value
uint8_t crc8le(uint8_t const message[], unsigned nBytes, uint8_t polynomial, uint8_t init);

/// CRC-16
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: CRC polynomial
/// @param init: starting crc value
/// @return CRC value
uint16_t crc16(uint8_t const message[], unsigned nBytes, uint16_t polynomial, uint16_t init);

/// CRC-16 CCITT
///
/// @param message[]: array of bytes to check
/// @param nBytes: number of bytes in message
/// @param polynomial: CRC polynomial
/// @param init: starting crc value
/// @return CRC value
uint16_t crc16_ccitt(uint8_t const message[], unsigned nBytes, uint16_t polynomial, uint16_t init);

/// compute bit parity of a single byte
///
/// @param inByte: single byte to check
/// @return 1 odd parity, 0 even parity
int byteParity(uint8_t inByte);

// buffer to hold localized timestamp YYYY-MM-DD HH:MM:SS
#define LOCAL_TIME_BUFLEN	32

/// Printable timestamp in local time
///
/// @param time_secs: 0 for now, or seconds since the epoch
/// @param buf: output buffer, long enough for YYYY-MM-DD HH:MM:SS
/// @return buf pointer (for short hand use as operator)
char* local_time_str(time_t time_secs, char *buf);

/// Convert Celsius to Fahrenheit
///
/// @param celsius: temperature in Celsius
/// @return temperature value in Fahrenheit
float celsius2fahrenheit(float celsius);


/// Convert Fahrenheit to Celsius
///
/// @param celsius: temperature in Fahrenheit
/// @return temperature value in Celsius
float fahrenheit2celsius(float fahrenheit);


/// Convert Kilometers per hour (kph) to Miles per hour (mph)
///
/// @param kph: speed in Kilometers per hour
/// @return speed in miles per hour
float kmph2mph(float kph);

/// Convert Miles per hour (mph) to Kilometers per hour (kmph)
///
/// @param mph: speed in Kilometers per hour
/// @return speed in kilometers per hour
float mph2kmph(float kph);


#endif /* INCLUDE_UTIL_H_ */
