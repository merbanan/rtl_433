/* Fine Offset Electronics sensor protocol
 *
 * Copyright (C) 2018  Joanne Dow
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the any OSS sanctioned license, GPL V2/3, Apache, BSD,
 * MIT, or others.
 */

#include "pch.h"

/*
 * Fine Offset Electronics WH31 Temperature/Humidity/time sensor protocol
 * Also sold by Ambient Weather and others.
 *
 * The sensor sends pairs of data packages. Temperature and humidity are easy
 * to decode.
 *
 * Battery state is not so obvious and is left out at the moment.
 * The sensor sends WWVB time back to the display unit. It seems to send this
 * very infrequently with no clear coding to it.
 *
 * This is a work in progress.
 */
/* Fine Offset Electronics WH31 Temperature/Humidity sensor protocol
*
* The sensor sends a package each ~64 s with a width of ~58 ms. The bits are PCM
* NRZ modulated with Frequency Shift Keying at a bit rate of about 18 kHz. Of the
* 58ms only 800 bits are valid, 45 ms. Within that burst the data is sent twice
* about 25.5 us apart. (about 456 bit times.) The latest time of interest is about
* 680 bits/85 bytes, into the buffer data.
*
* The signal is preceeded by 48 alternating 1 and 0 equalization bits with the last
* equalization bit being a zero. This is followed by the constant preamble to allow
* data synch, 2d d4 30. This is followed by a byte with a random number from power
* up, the ID nybble, 3 nybbles for temperature, and  a humidity byte. These are
* followed by several mystery bytes.
*
* The sensor ID is in the first three bits of the ID byte. The other 5 bits are
* unknown and do vary. The ID is one less than the channel number.
*
* Apparently the sensor sends time back to the base according to operating
* descriptions in the manual.
*
* Example:
* [00] {1028} ff ff ff 80 00 00 00 0a aa aa aa aa aa a2 dd 43 02 ae 2b a2 75 37 00 74 0b d0 c0 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 7f ff ff ff e0 00 00 00 01 55 55 55 55 55 54 5b a8 60 55 c5 74 4e a6 e0 07 40 bd 0c 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10
*	shifted left 4 bits								 2d d4 30 2a e2 ba 27 53 70 07 40 bd 0c
* [00] {1001} ff ff ff 00 00 00 00 2a aa aa aa aa aa 8b 75 0c 38 b0 ac 4a 04 f0 01 d6 49 cf 40 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 ff ff ff fe 00 00 00 00 55 55 55 55 55 55 16 ea 18 71 61 58 94 09 e0 03 ac 93 9e 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
*	shifted left two bits:							 2d d4 30 e2 c2 b1 28 13 c0 07 59 27 3d
* [00] {1028} 80 00 00 00 00 00 00 05 55 55 55 55 55 51 6e a1 85 de 95 41 1d 79 b8 21 2b 79 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 3f ff ff ff f0 00 00 00 00 aa aa aa aa aa aa 2d d4 30 bb d2 a8 23 af 37 04 25 6f 24 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10
*	shifted left 5 bits....						     2d d4 30 bb d2 a8 23 af 37 04 25 6f 24
* Reading: ID 1, 29.8C, 39%
*          ID 7, 28.9C, 40%
*          ID 5, 28.0C, 35%
*			  
*
* Extracted data:
*             ?? IT TT HH ?????????????????
* aa 2d d4 30 2a e2 ba 27 53 70 07 40 bd 0c
* aa 2d d4 30 e2 c2 b1 28 13 c0 07 59 27 3d
* aa 2d d4 30 bb d2 a8 23 af 37 04 25 6f 24
*
* ID = Sensor ID (based on 2 different sensors). Does not change at battery change.
*	   The ID in the examples are 1.
* T TT = Temperature (+40*10), 29.8C in the example.
* HH = Humidity, 39% in the example.
* CC = Checksum of previous 6 bytes (binary sum truncated to 8 bit)
* BB = Bitsum (XOR) of the 6 data bytes (high and low nibble exchanged)
*
*/

static int fineoffset_WH31_callback( bitbuffer_t *bitbuffer )
{
	data_t *data;
	char time_str[ LOCAL_TIME_BUFLEN ];

	// Validate package
	if ( bitbuffer->bits_per_row[ 0 ] < 400  )	// Strong signals may run together two frames
	{
		return 0;
	}

    if ( debug_output > 1 )
	{
        fprintf( stderr, "fineoffset_WH31\n" );
        bitbuffer_print( bitbuffer );
    }

	// Get time now
	local_time_str( 0, time_str );

	// Find a data package and extract data buffer
	static const uint8_t HEADER[] = { 0xAA, 0x2D, 0xD4, 0x30 };
	uint8_t buffer[ 10 ];
	unsigned bit_offset2 = 0;
	unsigned bit_offset = bitbuffer_search( bitbuffer, 0, 85, HEADER, sizeof( HEADER ) * 8 );    // Normal index is 367, skip some bytes to find faster
//	if bitbuffer->bits_per_row [ 0 ] > 500 we got both data packages in one bitbuffer package.
	bitbuffer_extract_bytes( bitbuffer, 0, bit_offset + 32, buffer, sizeof( buffer ) * 8 );

	if ( debug_output > 1 )
	{
		char raw_str[ 128 ];
		int size = sizeof( raw_str );
		for ( unsigned n = 0; n < sizeof( buffer ); n++ )
		{
			sprintf_s( raw_str + n * 3, size - n * 3, "%02x ", buffer[ n ]);
		}

		fprintf( stderr, "Fineoffset_WH31: Raw: %s @ bit_offset [%u] [%u] out of %u\n", raw_str, bit_offset, bit_offset2, bitbuffer->bits_per_row[ 0 ]);
	}

	// Decode data
	uint8_t id = (( buffer[ 1 ] >> 4 ) & 0x07 );
	float   temperature = (float)((uint16_t)( buffer[ 1 ] & 0x7 ) << 8 | buffer[ 2 ]) / 10.0f - 40.0f;
	uint8_t humidity = buffer[3];
	// This is a guess.
	bool battery_status = ( buffer[1] & 0x8 ) != 0;

	// Output data
    data = data_make("time",		  "",			 DATA_STRING,	time_str,
					 "model",		  "",			 DATA_STRING,	"Fine Offset Electronics, WH31",
					 "id",			  "Channel",	 DATA_INT,		id + 1,
					 "temperature_C", "Temperature", DATA_FORMAT,	"%.01f C",	 DATA_DOUBLE, temperature,
					 "humidity",	  "Humidity",	 DATA_FORMAT,	"%u %%",	 DATA_INT,	  humidity,
					 NULL);
    data_acquired_handler(data);

	return 1;
}



static char *output_fields_WH31[] =
{
	"time",
	"model",
	"id",
	"temperature_C",
	"humidity",
	NULL
};


r_device fineoffset_WH31 =
{
	/* .protocol_num	= */ 0,				// Place holder
	/* .name			= */ "Fine Offset Electronics, WH31 Temperature/Humidity Sensor",
	/* .modulation		= */ FSK_PULSE_PCM,
	/* .short_limit		= */ 58,	// Bit width = 58µs (measured across 580 samples / 40 bits / 250 kHz )
	/* .long_limit		= */ 58,	// NRZ encoding (bit width = pulse width)
	/* .reset_limit		= */ 59392,	//	59.392 ms apparent transmission duration
	/* .gap_limit		= */ 0,		// Place holder
	/* .sync_width		= */ 0,		// Place holder
	/* .tolerance		= */ 0,		// Place holder
	/* .json_callback	= */ &fineoffset_WH31_callback,
	/* .disabled		= */ 0,
	/* .demod_arg		= */ 0,
	/* .fields			= */ output_fields_WH31
};
