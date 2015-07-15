/**
 * Various utility functions for use by device drivers
 * 
 * Copyright (C) 2015 Tommy Vestermark
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "util.h"
#include <stdio.h>

uint8_t crc8(uint8_t const message[], unsigned nBytes, uint8_t polynomial) {
    uint8_t remainder = 0;	
    unsigned byte, bit;

    for (byte = 0; byte < nBytes; ++byte) {
        remainder ^= message[byte];
        for (bit = 0; bit < 8; ++bit) {
            if (remainder & 0x80) {
                remainder = (remainder << 1) ^ polynomial;
            }
            else {
                remainder = (remainder << 1);
            }
        }
    }
    return remainder;
}


void local_time_str(time_t time_secs, char *buf) {
	time_t etime;
	struct tm *tm_info;

	if (time_secs == 0) {
		time(&etime);
	} else {
		etime = time_secs;
	}

	tm_info = localtime(&etime);

	strftime(buf, LOCAL_TIME_BUFLEN, "%Y-%m-%d %H:%M:%S", tm_info);
}



// Test code
// gcc -I include/ -std=gnu99 -D _TEST src/util.c
#ifdef _TEST
int main(int argc, char **argv) {
	fprintf(stderr, "util:: test\n");

	uint8_t msg[] = {0x08, 0x0a, 0xe8, 0x80};

	fprintf(stderr, "util::crc8(): odd parity:  %02X\n", crc8(msg, 3, 0x80));
	fprintf(stderr, "util::crc8(): even parity: %02X\n", crc8(msg, 4, 0x80));

	return 0;
}
#endif /* _TEST */
