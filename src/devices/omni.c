/** @file
    Omni multi-sensor protocol, v1.2

    Copyright (C) 2025 H. David Todd <hdtodd@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/* clang-format off */
/**
Omni multisensor protocol.

The protocol is for the extensible wireless sensor 'omni'
-  Single transmission protocol
-  Flexible 64-bit data payload field structure
-  Extensible to a total of 16 possible multi-sensor data formats

The 'sensor' is actually a programmed microcontroller (e.g.,
Raspberry Pi Pico 2 or similar) with multiple possible data-sensor
attachments.  A message 'format' field indicates the format of the data
packet being sent.

NOTE: the rtl_433 decoder, omni.c, uses the "fmt" or "Format" field 
here, as transmitted by omni.c, to decode the incoming message.  
But, through omni.c, rtl_433 reports the packet format-field value
as "channel" in its published reporting (JSON, for example), 
in keeping with the standard nomenclature and order of field-name
precedence used within rtl_433 for data fields.  

The omni protocol is OOK modulated PWM with fixed period of 600μs
for data bits, preambled by four long startbit pulses of fixed period equal
to 1200μs. It is similar to the Lacrosse TX141TH-BV2.

A single data packet looks as follows:
1) preamble - 600μs high followed by 600μs low, repeated 4 times:

     ----      ----      ----      ----
    |    |    |    |    |    |    |    |
          ----      ----      ----      ----

2) a train of 80 data pulses with fixed 600μs period follows immediately:

     ---    --     --     ---    ---    --     ---
    |   |  |  |   |  |   |   |  |   |  |  |   |   |
         --    ---    ---     --     --    ---     -- ....

A logical 0 is 400μs of high followed by 200μs of low.
A logical 1 is 200μs of high followed by 400μs of low.

Thus, in the example pictured above the bits are 0 1 1 0 0 1 0 ...

The omni microcontroller sends 4 of identical packets of
4-pulse preamble followed by 80 data bits in a single burst, for a
total of 336 bits requiring ~212μs.

The last packet in a burst is followed by a postamble low
of at least 1250μs.

These 4-packet bursts repeat every 30 seconds. 

The message in each packet is 10 bytes / 20 nibbles:

    [fmt] [id] 16*[data] [crc8] [crc8]

- fmt is a 4-bit message data format identifier
- id is a 4-bit device identifier
- data are 16 nibbles = 8 bytes of data payload fields,
      interpreted according to 'fmt'
- crc8 is 2 nibbles = 1 byte of CRC8 checksum of the first 9 bytes:
      polynomial 0x97, init 0xaa

A format=0 message simply transmits the core temperature and input power
voltage of the microcontroller and is the format used if no data
sensor is present.  For format=0 messages, the message
nibbles are to be read as:

     fi tt t0 00 00 00 00 00 vv cc

     f: format of datagram, 0-15
     i: id of device, 0-15
     t: Pico 2 core temperature: °C *10, 12-bit, 2's complement integer
     0: bytes should be 0
     v: (VCC-3.00)*100, as 8-bit integer, in volts: 3V00..5V55 volts
     c: CRC8 checksum of bytes 1..9, initial remainder 0xaa,
        divisor polynomial 0x97, no reflections or inversions

A format=1 message format is provided as a more complete example.
It uses the Bosch BME688 environmental sensor as a data source.
It is an indoor-outdoor temperature/humidity/pressure sensor, and the
message packet has the following fields:
    indoor temp, outdoor temp, indoor humidity, outdoor humidity,
    barometric pressure, sensor power VCC.
The data fields are binary values, 2's complement for temperatures.
For format=1 messages, the message nibbles are to be read as:

     fi 11 12 22 hh gg pp pp vv cc

     f: format of datagram, 0-15
     i: id of device, 0-15
     1: sensor 1 temp reading (e.g, indoor),  °C *10, 12-bit, 2's complement integer
     2: sensor 2 temp reading (e.g, outdoor), °C *10, 12-bit, 2's complement integer
     h: sensor 1 humidity reading (e.g., indoor),  %RH as 8-bit integer
     g: sensor 2 humidity reading (e.g., outdoor), %RH as 8-bit integer
     p: barometric pressure * 10, in hPa, as 16-bit integer, 0..6553.5 hPa
     v: (VCC-3.00)*100, as 8-bit integer, in volts: 3V00..5V55 volts
     c: CRC8 checksum of bytes 1..9, initial remainder 0xaa,
            divisor polynomial 0x97, no reflections or inversions
*/
/* clang-format on */

#include "decoder.h"

#define initCRC 0xaa
#define OMNI_MSGFMT_00 0x00
#define OMNI_MSGFMT_01 0x01

static int omni_decode(r_device *, bitbuffer_t *);

// List the output fields for various message types
static char const *const output_fields_00[] = {
        "model",
        "fmt",
        "id",
	"temperature_C",
	"voltage_V",
        "payload",
        "mic",
        NULL,
};

static char const *const output_fields_01[] = {
        "model",
        "fmt",
        "id",
        "temperature_C",
        "temperature_2_C",
        "humidity",
        "light_level",
        "pressure_hPa",
	"voltage_V",
        "mic",
        NULL,
};

/* New format output_field_nn declaration goes here */

/* clang-format off */
r_device omni = {
        .name        = "Omni multisensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 200,  // short pulse is ~200 us
        .long_width  = 400,  // long pulse is ~400 us
        .sync_width  = 600,  // sync pulse is ~600 us
        .gap_limit   = 500,  // long gap (with short pulse) is ~400 us, sync gap is ~600 us
        .reset_limit = 1250, // maximum gap is 1250 us (long gap + longer sync gap on last repeat)
        .decode_fn   = &omni_decode,
        .fields      = output_fields_00,
};
/* clang-format on */

static int omni_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b;

    // Fields common to all message types
    int message_fmt, id;

    // Fields needed depending upon the message type
    double itemp_c, otemp_c, ihum, ohum, press, volts;

    // Find a row that's a candidate for decoding
    int r = bitbuffer_find_repeated_row(bitbuffer, 2, 80);

    if (r < 0 || bitbuffer->bits_per_row[r] > 82) {
        decoder_log(decoder, 1, __func__, "Omni: Invalid message");
        return DECODE_ABORT_LENGTH;
    };

    // OK, that's our message buffer for decoding
    b = bitbuffer->bb[r];

    // Validate the packet against the CRC8 checksum
    if (crc8(b, 9, 0x97, initCRC) != b[9]) {
        decoder_log(decoder, 1, __func__, "Omni: CRC8 checksum error");
        return DECODE_FAIL_MIC;
    }

    // OK looks like we have a valid packet.  What format?
    message_fmt = b[0] >> 4;
    id          = b[0] & 0x0F;

    // Decode that format, if we know it
    switch (message_fmt) {

    /*  Default to fmt=00 so message data are reported in hex
    default:
        decoder_log(decoder, 1, __func__, "Unknown message type");
        return DECODE_FAIL_SANITY;
    */
    default:
    case OMNI_MSGFMT_00:
        omni.fields = output_fields_00;
        char hexstring[50];
        char *ptr = &hexstring[0];
        for (int ij = 1; ij < 9; ij++)
            ptr += sprintf(ptr, "0x%02x ", b[ij]);
        itemp_c     = ((double)((int32_t)(((((uint32_t)b[1]) << 24) | ((uint32_t)(b[2]) & 0xF0) << 16)) >> 20)) / 10.0;
        volts       = ((double)(b[8])) / 100.0 + 3.00;
        // Make the data descriptor
        /* clang-format off */
	data = data_make(
	    "model",           "",                                               DATA_STRING, "Omni",
	    "id",              "Id",                                             DATA_INT,     id,
	    "channel",         "Format",                                         DATA_INT,     message_fmt,
	    "temperature_C"  , "Core Temperature",   DATA_FORMAT, "%.2f ˚C",     DATA_DOUBLE,  itemp_c, 
	    "voltage_V",       "VCC voltage",        DATA_FORMAT, "%.2f V",      DATA_DOUBLE,  volts,
            "payload",         "Payload",                                        DATA_STRING,  hexstring,
	    "mic",             "Integrity",                                      DATA_STRING,  "CRC",
	    NULL);
        /* clang-format on */
        break;

    case OMNI_MSGFMT_01:
        omni.fields = output_fields_01;
        itemp_c     = ((double)((int32_t)(((((uint32_t)b[1]) << 24) | ((uint32_t)(b[2]) & 0xF0) << 16)) >> 20)) / 10.0;
        otemp_c     = ((double)((int32_t)(((((uint32_t)b[2]) << 28) | ((uint32_t)b[3]) << 20)) >> 20)) / 10.0;
        ihum        = (double)b[4];
        ohum        = (double)b[5];
        press       = (double)(((uint16_t)(b[6] << 8)) | b[7]) / 10.0;
	volts       = ((double)(b[8])) / 100.0 + 3.00;
        // Make the data descriptor
        /* clang-format off */
	data = data_make(
	    "model",           "",                                               DATA_STRING, "Omni",
	    "id",              "Id",                                             DATA_INT,     id,
	    "channel",         "Format",                                         DATA_INT,     message_fmt,
	    "temperature_C"  , "Indoor Temperature", DATA_FORMAT, "%.2f ˚C",     DATA_DOUBLE,  itemp_c, 
	    "temperature_2_C", "Outdoor Temperature",DATA_FORMAT, "%.2f ˚C",     DATA_DOUBLE,  otemp_c, 
	    "humidity",        "Indoor Humidity",    DATA_FORMAT, "%.0f %%",     DATA_DOUBLE,  ihum,
	    "Light %",         "Light",              DATA_FORMAT, "%.0f%%",      DATA_DOUBLE,  ohum,
	    "pressure_hPa",    "BarometricPressure", DATA_FORMAT, "%.1f hPa",    DATA_DOUBLE,  press,
	    "voltage_V",       "VCC voltage",        DATA_FORMAT, "%.2f V",      DATA_DOUBLE,  volts,
	    "mic",             "Integrity",                                      DATA_STRING,  "CRC",
	    NULL);
        /* clang-format on */
        break;

	/* New decoders follow here */
	
    };

    // And output the field values
    decoder_output_data(decoder, data);
    return 1;
}
