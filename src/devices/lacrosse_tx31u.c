/** @file
    LaCrosse TX31U-IT protocol.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

*/
/**
Decoder for LaCrosse transmitter provided with the WS-1910TWC-IT product.
Branded with "The Weather Channel" logo.
https://www.lacrossetechnology.com/products/ws-1910twc-it

FCC ID: OMO-TX22U

FSK_PCM @915 MHz, 116usec/bit

## Protocol

Data format:

This transmitter uses a variable length protocol that includes 1-5 measurements
of 2 bytes each.  The first nibble of each measurement identifies the sensor.

    Sensor    Code    Encoding
    TEMP        0       BCD tenths of a degree C plus 400 offset.
                            EX: 0x0653 is 25.3 degrees C 
         TODO               EX:  0x0237 is -16.3 degrees C 
    HUMID       1       BCD % relative humidity.
                            EX: 0x1068 is 68%
    UNKNOWN     2       This is probably reserved for a rain gauge (TX32U-IT)
    WIND_SPEED  3       BCD time averaged wind speed in M/sec.
                            Second nibble is 0x1 if wind sensor was ever detected.
    WIND_GUST   4       BCD maximum wind speed in M/sec during last reporting interval.
                            Second nibble is 0x1 if wind sensor input is lost.



       a    a    a    a    2    d    d    4    a    2    e    5    0    6    5    3    c    0
    Bits :
    1010 1010 1010 1010 0010 1101 1101 0100 1010 0010 1110 0101 0000 0110 0101 0011 1100 0000
    Bytes num :
    ----1---- ----2---- ----3---- ----4---- ----5---- ----6---- ----7---- ----8---- ----N----
    ~~~~~~~~~~~~~~~~~~~ 2 bytes
    preamble (0xaaaa) 
                        ~~~~~~~~~~~~~~~~~~~ bytes 3 and 4
    sync word of 0x2dd4
                                            ~~~~ 1st nibble of byte 5
    sensor model (always 0xa)
                                                 ~~~~ ~~ 2nd nibble of byte 5 and 1st 2 bits of byte 6
    Random device id (6 bits)
                                                        ~ 3rd bit of byte 6
    Initial training mode
    Set for a while after power cycle, all sensors report
                                                         ~ 4th bit of byte 6
    no external sensor detected
                                                           ~ 5th bit of byte 6
    low battery indication
                                                            ~~~ bits 6,7,8 of byte 6
    count of sensors reporting (1 to 5)
                                                                ~~~~ 1st nibble of byte 7
    sensor code
                                                                     ~~~~ ~~~~ ~~~~ 2nd nibble of byte 7 and byte 8
    sensor reading (meaning varies, see above)

    ---
    --- repeat sensor code:reading as specified in count value above ---
    ---
                                                                                    ~~~~ ~~~~ last byte
    crc8 (poly 0x31 init 0x00) of bytes 5 thru (N-1)
                                                                          

## Developer's comments

*/

#include "decoder.h"

#define BIT(pos) ( 1<<(pos) )
#define CHECK_BIT(y, pos) ( ( 0u == ( (y)&(BIT(pos)) ) ) ? 0u : 1u )
#define SET_LSBITS(len) ( BIT(len)-1 ) // the first len bits are '1' and the rest are '0'
#define BF_PREP(y, start, len) ( ((y)&SET_LSBITS(len)) << (start) ) // Prepare a bitmask
#define BF_GET(y, start, len) ( ((y)>>(start)) & SET_LSBITS(len) )

#define TX31U_MIN_LEN_BYTES 9       // assume at least one measurement
#define TX31U_MAX_LEN_BYTES 20      // actually shouldn't be more than 18, but we'll be generous

static int lacrosse_tx31u_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{

    // There will only be one row
    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }

//TODO remove unnecessary logging or up the level to 2,3,4
    decoder_logf(decoder, 1, __func__, "Something detected, buffer is %d bits length", bitbuffer->bits_per_row[0]);

    // search for expected start sequence
    uint8_t const start_match[] = {0xaa, 0xaa, 0x2d, 0xd4}; // preamble + sync word (32 bits)
    unsigned int start_pos = bitbuffer_search(bitbuffer, 0, 0, start_match, sizeof(start_match)*8);
    if (start_pos >= bitbuffer->bits_per_row[0]) {
        return DECODE_ABORT_EARLY;
    }
    uint8_t msg_bytes = (bitbuffer->bits_per_row[0] - start_pos)/8;

    if (msg_bytes < TX31U_MIN_LEN_BYTES) { 
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bytes", msg_bytes);
        return DECODE_ABORT_LENGTH;
    } else if (msg_bytes > TX31U_MAX_LEN_BYTES) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bytes", msg_bytes);
        return DECODE_ABORT_LENGTH;
    } else {
        decoder_logf(decoder, 1, __func__, "packet length: %d", msg_bytes); //TODO should be level 2
    }

    decoder_log(decoder, 2, __func__, "LaCrosse TX31U-IT detected");

    uint8_t msg[TX31U_MAX_LEN_BYTES];
    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, msg_bytes*8);

// TODO remove
    for (int i=0; i<msg_bytes; ++i ) {
	decoder_logf(decoder, 1, __func__, "Byte %d = %02x", i, msg[i]);
    }
//

    //TODO int model = BF_GET(msg[4], 4, 4);
    int sensor_id = (BF_GET(msg[4], 0, 4) << 2) | BF_GET(msg[5], 6, 2);
    //TODO int training = CHECK_BIT(msg[5], 5);
    int no_ext_sensor = CHECK_BIT(msg[5], 4); 
    int battery_low = CHECK_BIT(msg[5], 3); 
    int measurements = BF_GET(msg[5], 0, 3);

//TODO    decoder_logf(decoder, 1, __func__, "model %d, id = 0x%02x, training = %d, no_ext_sensor = %d", model, sensor_id, training, no_ext_sensor );
//TODO    decoder_logf(decoder, 1, __func__, "battery_low = %d, measurements = %d", battery_low, measurements );

    // Check message integrity.  
    int r_crc = msg[6+measurements*2];
    int c_crc = crc8(&msg[4], 2 + measurements*2, 0x31, 0x00);
    if (r_crc != c_crc) {
	decoder_logf(decoder, 1, __func__, "LaCrosse TX31U-IT bad CRC: calculated %02x, received %02x", c_crc, r_crc);
	return DECODE_FAIL_MIC;
    }

    /* clang-format off */
    data_t *data = data_make(
	    "model",            "",             DATA_STRING, "LaCrosse-TX31U-IT",
	    "id",               "",             DATA_INT,    sensor_id,
	    "battery_ok",       "Battery",      DATA_INT,    !battery_low,
	    NULL);

    // decode what measurements we get and append them.  All measurements are BCD coded.
    enum sensor_type { TEMP=0, HUMIDITY, RAIN, WIND_SPEED, WIND_GUST };
    for (int m=0; m<measurements; ++m ) {
        uint8_t type = BF_GET(msg[6+m*2], 4, 4 );
        uint8_t bcd1 = BF_GET(msg[6+m*2], 0, 4 );
        uint8_t bcd2 = BF_GET(msg[7+m*2], 4, 4 );
        uint8_t bcd3 = BF_GET(msg[7+m*2], 0, 4 );
        switch (type) {
	    case TEMP: { 
		float temp_c = 10*bcd1 + bcd2 + 0.1f*bcd3 - 40.0f;
		data = data_append( data,
		    "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
		    NULL);
	    } break;
	    case HUMIDITY: {
		int humidity = 100*bcd1 + 10*bcd2 + bcd3;
		data = data_append( data,
		    "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
		    NULL);
	    } break;
	    case RAIN: {
		int raw_rain = 100*bcd1 + 10*bcd2 + bcd3;
		if ( !no_ext_sensor && raw_rain > 0) { // most of these do not have rain gauges.  Surpress output if zero.
		    data = data_append( data,
			"rain",         "raw_rain",      DATA_FORMAT, "%03x", DATA_INT, raw_rain,
			NULL);
		}
	    } break;
	    case WIND_SPEED: {
		int wind_sensor_detected = CHECK_BIT(bcd1, 0); // a sensor is attached
		if ( !no_ext_sensor && wind_sensor_detected ) {
		    float wind_speed = (10*bcd2 + bcd3) * 0.1f;
		    data = data_append( data,
			"wind_speed",  "Wind speed",    DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, wind_speed,
			NULL);
		}
	    } break;
	    case WIND_GUST: {
                int wind_input_lost = CHECK_BIT(bcd1, 0); // a sensor was attched, but now not detected
		if ( !no_ext_sensor && !wind_input_lost ) {
		    float wind_gust = (10*bcd2 + bcd3) * 0.1f;
		    data = data_append( data,
			"wind_gust",  "Wind gust",    DATA_FORMAT, "%.1f km/h",  DATA_DOUBLE, wind_gust,
			NULL);
                }
	    } break;
	    default:
		decoder_logf(decoder, 1, __func__, "LaCrosse TX31U-IT unknown sensor type %d", type);
	    break;
	}
    }

    data = data_append( data,
	    "mic",              "Integrity",    DATA_STRING, "CRC",
	    NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "low_battery",
        "temperature",
        "humidity",
        "wind_speed",
        "wind_gust",
        "mic",
        NULL,
};

// Receiver for the TX31U-IT 
r_device lacrosse_tx31u = {
        .name        = "LaCrosse TX31U-IT, The Weather Channel WS-1910TWC-IT",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 116,
        .long_width  = 116,
        .reset_limit = 20000,
        .decode_fn   = &lacrosse_tx31u_decode,
        .fields      = output_fields,
};
