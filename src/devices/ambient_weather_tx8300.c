/* Ambient Weather TX-8300 Thermometer
 * contributed by Roger
*/

#include <byteswap.h>
#include "rtl_433.h"
#include "data.h"
#include "util.h"

/* 
Packet format (74 bits):
   [2 bit preamble] [1 bit start bit] [31 bit payload] [1 bit start bit] [31 bit payload (inverted)] [8 bit CRC]   
   HH1PPPPP PPPPPPPP PPPPPPPP PPPPPPPP PP1QQQQQ QQQQQQQQ QQQQQQQQ QQQQQQQQ QQCCCCCC CC   
   
Preamble format (2 bits):
    [1 bit (0)] [1 bit rolling count]    
    0R

Payload format (31 bits):
   [9 bit unknown (humidity?)] [2 bit channel number] [1 bit negative flag] [7 bit ID]  [12 bit BCD temperature (C, 1 decimal point)]   
   UUUUUUUU UNNSIIII IIITTTTT TTTTTTT
*/



static const int BITS_PER_MSG = 74; 

static int
ambient_weather_tx8300_callback(bitbuffer_t *bitbuffer)
{    
    uint32_t payload = 0; 
    uint32_t payload_inv = 0;     
    
    int deviceID; 
    int channel;
    float temperature;
    int unknown;
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data; 
    int count;
    int crc;
    
	if(bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] != BITS_PER_MSG) {
	    if (debug_output > 1) {
	        fprintf(stderr, "wrong number of bits");
	     }
	        
		return 0; // Unrecognized data
    }
    
    /* This could be used for extra validation but disabled until the purpose
       of the bits are better understood (e.g. batter status) */
    /*if(bitrow_get_bit(bitbuffer->bb[0], 0) || !bitrow_get_bit(bitbuffer->bb[0], 2) || !bitrow_get_bit(bitbuffer->bb[0], 34)) {
        if (debug_output > 1) {
            fprintf(stderr, "marker bits missing");
         }
            
        return 0; // Unrecognized data
    }*/     
        
    // get payload and inverted payload
    bitbuffer_extract_bytes(bitbuffer, 0, 3, (uint8_t *)&payload, 31);
    bitbuffer_extract_bytes(bitbuffer, 0, 35, (uint8_t *)&payload_inv, 31);
    
    // fix LSB as previous function may copy it
    payload &= 0xFEFFFFFF;
    payload_inv |= 0x01000000;
    
    if(payload != ~payload_inv) {
        if (debug_output > 1) {
            fprintf(stderr, "inverted payload mismatch");
        }
        
        return 0; // Unrecognized data
    } 
    
    // switch endianess to match above bits   
    payload = __bswap_32(payload);
    
    // drop extra bit
    payload >>= 1;    

    // BCD temperature
    temperature  =  10 * ((payload & 0x0F00) >> 8);
    temperature +=        (payload & 0x00F0) >> 4;
    temperature += 0.1 * ((payload & 0x000F) >> 0);
    
    // check negative flag
    if(bitrow_get_bit(bitbuffer->bb[0], 14))
        temperature *= -1;
    
    deviceID = (payload >> 12) & 0x7F; 
    channel  = (payload >> 20) & 0x03;
    unknown  = (payload >> 22) & 0x01FF;
    count    = bitrow_get_bit(bitbuffer->bb[0], 1) & 0x01;
    bitbuffer_extract_bytes(bitbuffer, 0, 66, (uint8_t *)&crc, 8);;

    local_time_str(0, time_str);
    data = data_make(
            "time",           "",              DATA_STRING, time_str,
            "model",          "",              DATA_STRING, "Ambient Weather TX-8300 Thermometer",
            "device",         "ID",            DATA_INT,    deviceID,
            "channel",        "Channel",       DATA_INT,    channel,
            "count",          "Rolling count", DATA_INT,    count,
            "temperature_C",  "Temperature",   DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "unknown",        "Unknown",       DATA_FORMAT, "0x%03x", DATA_INT, unknown,
            "crc",            "CRC",           DATA_FORMAT, "0x%02x", DATA_INT, crc,
            NULL);
    data_acquired_handler(data);

    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "device",
    "channel",
    "count",
    "temperature_C",
    "unknown",
    "crc",
    NULL
};

r_device ambient_weather_tx8300 = {
    .name          = "Ambient Weather TX-8300 Thermometer",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = 3000,
    .long_limit    = 5000,
    .reset_limit   = 6000,
    .json_callback = &ambient_weather_tx8300_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields
};
