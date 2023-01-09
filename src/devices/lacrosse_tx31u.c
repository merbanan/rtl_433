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
    TEMP      0       BCD tenths of a degree C plus 400 offset.
                      EX: 0x0653 is 25.3 degrees C 
         TODO             EX:  0x0237 is -16.3 degrees C 
    HUMID     1       BCD % relative humidity.
                      EX: 0x1068 is 68%
    UNKNOWN   2       This is probably reserved for a rain gauge (TX32U-IT)
    WIND_AVG  3       BCD time averaged wind speed in M/sec.
                      Second nibble is 0x1 if wind sensor was ever detected.
    WIND_MAX  4       BCD maximum wind speed in M/sec during last reporting interval.
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

static int lacrosse_tx31u_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
 /*
        // remove preamble and keep only five octets
        uint8_t b[5];
        bitbuffer_extract_bytes(bitbuffer, row, start_pos + 20, b, 40);

        // Check message integrity
        int r_crc = b[4];
        int c_crc = crc8(b, 4, 0x31, 0x00);
        if (r_crc != c_crc) {
            decoder_logf(decoder, 1, __func__, "LaCrosse TX29/35 bad CRC: calculated %02x, received %02x", c_crc, r_crc);
            // reject row
            continue; // DECODE_FAIL_MIC
        }

        // message "envelope" has been validated, start parsing data
        int sensor_id   = ((b[0] & 0x0f) << 2) | (b[1] >> 6);
        float temp_c    = 10 * (b[1] & 0x0f) + 1 * ((b[2] >> 4) & 0x0f) + 0.1f * (b[2] & 0x0f) - 40.0f;
        int new_batt    = (b[1] >> 5) & 1;
        int battery_low = b[3] >> 7;
        int humidity    = b[3] & 0x7f;

        data_t *data;
        if ((humidity == LACROSSE_TX29_NOHUMIDSENSOR) || (humidity == LACROSSE_TX25_PROBE_FLAG)) {
            if (humidity == LACROSSE_TX25_PROBE_FLAG)
                sensor_id += 0x40;      // Change ID to distinguish between the main and probe channels
            data = data_make(
                    "model",            "",             DATA_STRING, (device29or35 == 29 ? "LaCrosse-TX29IT" : "LaCrosse-TX35DTHIT"),
                    "id",               "",             DATA_INT,    sensor_id,
                    "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                    "newbattery",       "NewBattery",   DATA_INT,    new_batt,
                    "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
        }
        else {
            data = data_make(
                    "model",            "",             DATA_STRING, (device29or35 == 29 ? "LaCrosse-TX29IT" : "LaCrosse-TX35DTHIT"),
                    "id",               "",             DATA_INT,    sensor_id,
                    "battery_ok",       "Battery",      DATA_INT,    !battery_low,
                    "newbattery",       "NewBattery",   DATA_INT,    new_batt,
                    "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    "mic",              "Integrity",    DATA_STRING, "CRC",
                    NULL);
        }

        decoder_output_data(decoder, data);
        events++;
    }
*/
return DECODE_ABORT_LENGTH; //TODO
    return 1;
}

static int lacrosse_tx31u_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    // preamble + sync word - 32 bits
    uint8_t const preamble[] = {0xaa, 0xaa, 0x2d, 0xd4};

    int rtn = 0;

    // There will only be one row
    if (bitbuffer->num_rows > 1) {
        decoder_logf(decoder, 1, __func__, "Too many rows: %d", bitbuffer->num_rows);
        return DECODE_FAIL_SANITY;
    }

    //TODO
    decoder_logf(decoder, 1, __func__, "Something detected, buffer is %d bits length", bitbuffer->bits_per_row[0]);

    // search for preamble
    unsigned int start_pos = bitbuffer_search(bitbuffer, 0, 0, preamble, sizeof(preamble)*8);
    if (start_pos >= bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;

    decoder_logf(decoder, 1, __func__, "LaCrosse TX31U-IT detected, buffer is %d bits length", bitbuffer->bits_per_row[0]);

    uint8_t msg[20];
    uint8_t lenbytes = (bitbuffer->bits_per_row[0] - start_pos)/8;
    decoder_logf(decoder, 1, __func__, "lenbytes = %d", lenbytes);
    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, lenbytes*8);
    for (int i=0; i<lenbytes; ++i ) {
	decoder_logf(decoder, 1, __func__, "Byte %d = %02x", i, msg[i]);
    }
	decoder_logf(decoder, 1, __func__, "Count %d, Code_1 = %d, Temp = %x.%02x",
		                	msg[5] & 0x7, ((msg[6]>>4) & 0xf) - 0x40, msg[6] & 0xf, msg[7] );
    /*
    int msg_len = bitbuffer->bits_per_row[0];
    if (msg_len < 200) { // allows shorter preamble for LTV-R3
        decoder_logf(decoder, 1, __func__, "Packet too short: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    } else if (msg_len > 272) {
        decoder_logf(decoder, 1, __func__, "Packet too long: %d bits", msg_len);
        return DECODE_ABORT_LENGTH;
    } else {
        decoder_logf(decoder, 1, __func__, "packet length: %d", msg_len);
    }
    */

    rtn = lacrosse_tx31u_decode(decoder, bitbuffer);

    return rtn;
}

static char *output_fields[] = {
        "model",
        "id",
        "low_battery",
        "temperature",
        "humidity",
        "avg_wind_spd",
        "peak_wind_spd",
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
        .decode_fn   = &lacrosse_tx31u_callback,
        .fields      = output_fields,
};
