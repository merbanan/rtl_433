/* Ambient Weather TX-8300 Thermometer
 * contributed by Roger

Packet format (74 bits):
   [2 bit preamble] [1 bit start bit] [31 bit payload] [1 bit start bit] [31 bit payload (inverted)] [8 bit CRC]   
   HH1PPPPP PPPPPPPP PPPPPPPP PPPPPPPP PP1QQQQQ QQQQQQQQ QQQQQQQQ QQQQQQQQ QQCCCCCC CC   
   
Preamble format (2 bits):
    [1 bit (0)] [1 bit rolling count]    
    0R

Payload format (31 bits):
   [9 bit unknown (humidity?)] [2 bit channel number] [1 bit negative flag] [7 bit ID]  [12 bit BCD temperature (C, 1 decimal point)]   
   UUUUUUUU UNNSIIII IIITTTTT TTTTTTT
   
   
   0UUUUUUU UUNNSIII IIIITTTT TTTTTTTT
*/

#include <byteswap.h>
#include "rtl_433.h"
#include "data.h"
#include "util.h"

static const int BITS_PER_MSG = 74; 

static int
ambientweather_tx8300_callback(bitbuffer_t *bitbuffer)
{   
    uint8_t b[9];  
    
    int deviceID = 0; 
    int channel = 0;
    float temperature = 0;
    int unknown = 0;
    int count = 0;
    int crc = 0;
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data; 
    
    if(bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] != BITS_PER_MSG) {
        if (debug_output > 1) {
            fprintf(stderr, "wrong number of bits\n");
        }

        return 0; // Unrecognized data
    } 
    
    // get both payload and inverted payload and clear erroneous bits
    bitbuffer_extract_bytes(bitbuffer, 0, 2, b, 72);
    b[0] &= 0x7F;
    b[4] |= 0x80; 
    
    uint8_t tmp;
    
    // check if payload and inverted payload match
    for(int i = 0; i < 4; ++i) {
        tmp = ~b[i + 4];
        
        if(b[i] != tmp) {
            if (debug_output > 1) {
                fprintf(stderr, "inverted payload mismatch\n");
            }
            
            return 0; // Unrecognized data
        }
    }  

    // Convert temperature from BCD
    temperature = 10 * (b[2] & 0x0F) + 1 * ((b[3] & 0xF0) >> 4) + 0.1 * (b[3] & 0x0F);
    
    // check negative flag
    if(bitrow_get_bit(bitbuffer->bb[0], 14))
        temperature *= -1;
    
    // get remaining fields  
    count    = bitrow_get_bit(bitbuffer->bb[0], 1);
    crc      = b[8];
    deviceID = (b[2] >> 4) | ((b[1] & 0x07) << 4);
    channel  = (b[1] & 0x30) >> 4;
    unknown  = (b[1] >> 6) | (b[0] << 2) |  ((b[0] & 0x40) << 2);

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

r_device ambientweather_tx8300 = {
    .name          = "Ambient Weather TX-8300 Thermometer",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = 3000,
    .long_limit    = 5000,
    .reset_limit   = 6000,
    .json_callback = &ambientweather_tx8300_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields
};
