/** @file
    Auriol AFT 77 B2 sensor protocol.
*/
/** @fn int auriol_afT77_b2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
Auriol AFT 77 B2 protocol. The sensor can be bought at Lidl.

The sensor sends 68 bits at least 3 times, before the packets are 9 sync pulses
of 1900us length.
The packets are ppm modulated (distance coding) with a pulse of ~488 us
followed by a short gap of ~488 us for a 0 bit or a long ~976 us gap for a
1 bit, the sync gap is ~1170 us.

The data is grouped in 17 nibbles

    [prefix] [0x05] [0x0C] [id0] [id1] [0x00] [flags] [sign] [temp0] [temp1] [temp2]
    [0x00] [0x00] [sum] [sum] [lsrc] [lsrc]

- prefix: 4 bit fixed 1010 (0x0A) ignored when calculating the checksum and lsrc
- id: 8 bit a random id that is generated when the sensor starts
- flags(1): was set at first start and reset after a restart
- sign(3): is 1 when the reading is negative
- temp: a BCD number scaled by 10, 175 is 17.5C
- sum: 8 bit sum of the previous bytes
- lsrc: Galois LFSR, bits reflected, gen 0x83, key 0xEC

*/

#include "decoder.h"

#define GEN 0x83
#define KEY 0xEC

#define LEN 8

static uint8_t sum( uint8_t frame[], int len )
{
   uint8_t result = 0 ;

   for( int i = 0; i < len; i++ )
      result += frame[i] ;

   return result ;
}

static uint8_t lsrc( uint8_t frame[], int len )
{
   uint8_t result = 0 ;
   uint8_t key    = KEY ;

   for( int i = 0; i < len; i++ )
   {
      uint8_t byte = frame[i] ;

      for( uint8_t mask = 0x80; mask > 0; mask >>= 1 )
      {
	 if( (byte & mask) != 0 )
	    result ^= key ;

	 if( (key & 1) != 0 )
	    key = (key >> 1) ^ GEN ;
	 else
	    key = (key >> 1) ;
      }
   }

   return result ;
}

static int auriol_aft77_b2_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;

    // Check the length
    if( bitbuffer->bits_per_row[0] != 68 )
       return DECODE_ABORT_EARLY;

    // Check the prefix
    uint8_t *ptr = bitbuffer->bb[0] ;

    if( *ptr != 0xA5 )
       return DECODE_ABORT_EARLY;

    uint8_t frame[LEN] ;

    // Drop the prefix
    for( int i = 0; i < LEN; i++ )
        frame[i] = (ptr[i] << 4) | (ptr[i+1] >> 4) ;

    // Check the sum
    if( sum(frame,6) != frame[6] )
       return DECODE_FAIL_SANITY;

    // Check the lsrc
    if( lsrc(frame,6) != frame[7] )
       return DECODE_FAIL_SANITY;

    int id       = frame[1] ;

    int temp_raw = (ptr[4] >> 4) * 100 + (ptr[4] & 0x0F) * 10 + (ptr[5] >> 4) ;

    if( (ptr[3] & 0x08) != 0 )
       temp_raw = -temp_raw ;

    /* clang-format off */
    data = data_make(
            "model",         "",            DATA_STRING, _X("Auriol AFT 77 B2","Auriol sensor"),
	    "id",            "",            DATA_INT, id,
            "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_raw * 0.1,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "temperature_C",
        NULL,
};

r_device auriol_aft77b2 = {
        .name        = "Auriol AFT 77 B2 temperature sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 500,
        .long_width  = 1000,
        .gap_limit   = 1200,
        .reset_limit = 1800,
        .decode_fn   = &auriol_aft77_b2_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
