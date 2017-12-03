/* Maverick ET-73x BBQ Sensor
 *
 * Copyright © 2016 gismo2004
 * Credits to all users of mentioned forum below!
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
*/

/**The thermometer transmits 4 identical messages every 12 seconds at 433.92 MHz,
 * using on-off keying and 2000bps Manchester encoding,
 * with each message preceded by 8 carrier pulses 230uS wide and 5ms apart.
 *
 * Each message consists of 26 nibbles (104 bits total),
 * which can each only have the value of 0x5, 0x6, 0x9, or 0xA.
 * For nibble 24 some devices are sending 0x1 or 0x2
 *
 * Assuming MSB first and falling edge = 1.
 *
 * quarternary conversion of message needed:
 * 0x05 = 0
 * 0x06 = 1
 * 0x09 = 2
 * 0x0A = 3
 *
 * Message looks like this:
 * a = Header (0xAA9995)
 * b = device state (2=default; 7=init)
 * c = temp1 (need to substract 532)
 * d = temp2 (need to substract 532)
 * e = checksum (the checksum gets renewed on a device reset, and represents a kind of session_id)
 *
 * nibble: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
 * msg:    a a a a a a b b c c c  c  c  d  d  d  d  d  e  e  e  e  e  e  e  e
 *
 * further information can be found here: https://forums.adafruit.com/viewtopic.php?f=8&t=25414
**/


#include "rtl_433.h"
#include "util.h"

#define MAV_MESSAGE_LENGTH 104
#define TEMPERATURE_START_POSITION_S1 8
#define TEMPERATURE_START_POSITION_S2 13
#define TEMPERATURE_NIBBLE_COUNT 5


//here we extract bitbuffer values, for easy data handling
static void convert_bitbuffer(bitbuffer_t *bitbuffer, unsigned int *msg_converted, char *msg_hex_combined) {
    //quarternary convertion
    unsigned int quart_convert[16] ={0,0,0,0,0,0,1,0,0,2,3,0,0,0,0,0};
    int i;
    for(i = 0; i < 13; i++) {
        char temp[3];
        sprintf(temp, "%02x", bitbuffer->bb[0][i]);
        msg_hex_combined[i*2] = temp[0];
        msg_hex_combined[i*2+1] = temp[1];

        msg_converted[i*2] = quart_convert[bitbuffer->bb[0][i] >> 4];
        msg_converted[i*2+1] = quart_convert[bitbuffer->bb[0][i] & 0xF];
    }

    msg_hex_combined[26]='\0';
    if(debug_output) {
        fprintf(stderr, "final hex string: %s\n",msg_hex_combined);
        fprintf(stderr, "final converted message: ");
        for(i = 0; i <= 25; i++) fprintf(stderr, "%d",msg_converted[i]);
        fprintf(stderr, "\n");
    }
}

static float get_temperature(unsigned int *msg_converted, unsigned int temp_start_index){
    //default offset
    float temp_c = -532.0;
    int i;

    for(i=0; i < TEMPERATURE_NIBBLE_COUNT; i++) {
        temp_c += msg_converted[temp_start_index+i] * (1<<(2*(4-i)));
    }

    return temp_c;
}


//changes when thermometer reset button is pushed or powered on.
static char* get_status(unsigned int *msg_converted) {
    int stat = 0;
    char* retval = "unknown";

    //nibble 6 - 7 used for status
    stat += msg_converted[6] * (1<<(2*(1)));
    stat += msg_converted[7] * (1<<(2*(0)));

    if(stat == 2)
        retval = "default";

    if(stat == 7)
        retval = "init";

    if(debug_output)
        fprintf(stderr, "device status: \"%s\" (%d)\n", retval, stat);

    return retval;
}


static uint32_t checksum_data(unsigned int *msg_converted) {
    int32_t checksum = 0;
    int i;

    //nibble 6 - 17 used for checksum
    for(i=0; i<=11; i++) {
        checksum |= msg_converted[6+i] << (22 - 2*i);
    }

    if(debug_output)
        fprintf(stderr, "checksum data = %x\n", checksum);

    return checksum;
}


static uint32_t checksum_received(unsigned int *msg_converted, char *msg_hex_combined) {
    uint32_t checksum = 0;
    int i;

    //nibble 18 - 25 checksum info from device
    for(i=0; i<=7; i++) {
         checksum |= msg_converted[18+i] << (14 - 2*i);
    }

    if(msg_hex_combined[24]=='1' || msg_hex_combined[24]=='2') {
        checksum |= (msg_converted[25]&1) << 3;
        checksum |= (msg_converted[25]&2) << 1;

        if(msg_hex_combined[24]=='2')
            checksum |= 0x02;
    }
    else {
        checksum |= msg_converted[24] << 2;
        checksum |= msg_converted[25];
    }

    if(debug_output)
        fprintf(stderr, "checksum received= %x\n", checksum);

    return checksum;
}

static uint16_t shiftreg(uint16_t currentValue) {
    uint8_t msb = (currentValue >> 15) & 1;
    currentValue <<= 1;

    // Toggle pattern for feedback bits
    // Toggle, if MSB is 1
    if (msb == 1)
        currentValue ^= 0x1021;

    return currentValue;
}

static uint16_t calculate_checksum(uint32_t data) {
    //initial value of linear feedback shift register
    uint16_t mask = 0x3331;
    uint16_t csum = 0x0;
    int i;
    for(i = 0; i < 24; ++i) {
        //data bit at current position is "1"
        //do XOR with mask
        if((data >> i) & 0x01)
            csum ^= mask;

        mask = shiftreg(mask);
    }
    return csum;
}

static int maverick_et73x_callback(bitbuffer_t *bitbuffer) {
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    int32_t session_id;
    char msg_hex_combined[27];
    unsigned int msg_converted[26];

    //we need an inverted bitbuffer
    bitbuffer_invert(bitbuffer);

    if(bitbuffer->num_rows != 1)
        return 0;

    //check correct data length
    if(bitbuffer->bits_per_row[0] != MAV_MESSAGE_LENGTH)
        return 0;

    //check for correct header (0xAA9995)
    if((bitbuffer->bb[0][0] != 0xAA || bitbuffer->bb[0][0] != 0xaa ) || bitbuffer->bb[0][1] != 0x99 || bitbuffer->bb[0][2] != 0x95)
        return 0;

    //convert hex values into quardinary values
    convert_bitbuffer(bitbuffer, msg_converted, msg_hex_combined);

    //checksum is used to represent a session. This means, we get a new session_id if a reset or battery exchange is done.
    session_id = (calculate_checksum(checksum_data(msg_converted)) & 0xffff) ^ checksum_received(msg_converted, msg_hex_combined);

    if(debug_output)
        fprintf(stderr, "checksum xor: %x\n", session_id);

    local_time_str(0, time_str);

    data = data_make("time",           "",                      DATA_STRING,                         time_str,
                     "brand",          "",                      DATA_STRING,                         "Maverick",
                     "model",          "",                      DATA_STRING,                         "ET-732/ET-733",
                     "id",             "Session_ID",            DATA_INT,                            session_id,
                     "status",         "Status",                DATA_STRING,                         get_status(msg_converted),
                     "temperature_C1", "TemperatureSensor1",    DATA_FORMAT, "%.02f C", DATA_DOUBLE, get_temperature(msg_converted,TEMPERATURE_START_POSITION_S1),
                     "temperature_C2", "TemperatureSensor2",    DATA_FORMAT, "%.02f C", DATA_DOUBLE, get_temperature(msg_converted,TEMPERATURE_START_POSITION_S2),
                     "mic", "Integrity", DATA_STRING, "CHECKSUM",
                     NULL);
    data_acquired_handler(data);

    return bitbuffer->num_rows;
}

static char *output_fields[] = {
    "time",
    "brand"
    "model"
    "id"
    "status",
    "temperature_C1",
    "temperature_C2",
    "mic",
    NULL
};


r_device maverick_et73x = {
    .name           = "Maverick ET-732/733 BBQ Sensor",
    .modulation     = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit    = 230,
    .long_limit     = 0, //not used
    .reset_limit    = 4000,
    .json_callback  = &maverick_et73x_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};
