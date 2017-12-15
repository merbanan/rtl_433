/* Interlogix/GE Wireless Device Decoder
 *
 * Copyright © 2017 Brent Bailey - bailey.brent@gmail.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"


/*
 * Interlogix/GE Wireless 315.508 mhz Devices
 *
 * Found actual frequency to be -f 319511500
 *
 * Deocding done per us patent #5761206
 * https://www.google.com/patents/US5761206
 *
 * Protocol Bits
 * ________________________________
 * 00-02 976 uS RF front porch pulse
 * 03-14 12 sync pulses, logical zeros
 * 15 start pulse, logical one
 * 16-35 20 bit sensor identification code (ID bits 0-19)
 * 36-39 4 bit device type code (DT bits 0-3)
 * 40-42 3 bit trigger count (TC bit 0-2)
 * 43 low battery bit
 * 44 F1 latch bit NOTE: F1 latch bit and debounce are reversed.  Typo or endianess issue?
 * 45 F1 debounced level
 * 46 F2 latch bit
 * 47 F2 debounced level
 * 48 F3 latch bit (cover latch for contact sensors)
 * 49 F3 debounced level
 * 50 F4 latch bit
 * 51 F4 debounced level
 * 52 F5 positive latch bit
 * 53 F5 debounced level
 * 54 F5 negative latch bit
 * 55 even parity over odd bits 15-55
 * 56 odd parity over even bits 16-56
 * 57 zero/one, programmable
 * 58 RF on for 366 uS (old stop bit)
 * 59 one
 * 60-62 modulus 8 count of number of ones in bits 15-54
 * 63 zero (new stop bit)
 *
 * Protocol Description
 * ________________________________
 * Bits 00 to 02 are a 976 ms RF front porch pulse, providing a wake up period that allows the
 *      system controller receiver to synchronize with the incoming packet.
 * Bits 3 to 14 include 12 sync pulses, e.g., logical 0's, to synchronize the receiver.
 * Bit 15 is a start pulse, e.g., a logical 1, that tells the receiver that data is to follow.
 * Bits 16-58 provide information regarding the transmitter and associated sensor. In other
 *      embodiments, bits 16-58 may be replaced by an analog signal.
 * Bits 16 to 35 provide a 20-bit sensor identification code that uniquely identifies the particular
 *      sensor sending the message. Bits 36 to 39 provide a 4 bit device-type code that identifies the
 *      specific-type of sensor, e.g., smoke, PIR, door, window, etc. The combination of the sensor
 *      bits and device bits provide a set of data bits.
 * Bits 40 through 42 provide a 3-bit trigger count that is incremented for each group of message
 *      packets. The trigger count is a simple but effective way for preventing a third party from
 *      recording a message packet transmission and then re-transmitting that message packet
 *      transmission to make the system controller think that a valid message packet is being transmitted.
 * Bit 43 provides the low battery bit.
 * Bits 44 through 53 provide the latch bit value and the debounced value for each of the five inputs
 *      associated with the transmitter. For the F5 input, both a positive and negative latch bit are provided.
 * Bit 55 provides even parity over odd bits 15 to 55.
 * Bit 56 provides odd parity over even bits 16 to 56.
 * Bit 57 is a programmable bit that can be used for a variety of applications, including providing an
 *      additional bit that could be used for the sensor identification code or device type code.
 * Bit 58 is a 366 ms RF on signal that functions as the "old" stop bit. This bit provides compatibility with
 *      prior system controllers that may be programmed to receive a 58-bit message.
 * Bit 59 is a logical 1.
 * Bits 60 to 62 are a modulus eight count of the number of 1 bits in bits 15 through 54, providing enhanced
 *      error detection information to be used by the system controller. Finally, bit 63 is the "new" stop bit,
 *      e.g., a logical 0, that tells the system controller that it is the end of the message packet.
 *
 *
 *
 */

#define INTERLOGIX_CRC_POLY    0x07
#define INTERLOGIX_CRC_INIT    0x00
#define INTERLOGIX_MSG_BIT_LEN 48

//only searching for 0000 0001 (bottom 8 bits of the 13 bits preamble)
static unsigned char preamble[1] = { 0x01 };

//assumes little endian - usage printBits(sizeof(i), &i);
void printBits(size_t const size, void const * const ptr)
{
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j;

    for (i=size-1;i>=0;i--)
    {
        for (j=7;j>=0;j--)
        {
            byte = (b[i] >> j) & 1;
            fprintf(stderr, "%u", byte);
        }
    }
    fprintf(stderr, " ");
}

char * int2bin(uint8_t i)
{
    size_t bits = sizeof(uint8_t) * 8;

    char * str = malloc(bits + 1);
    if(!str) return NULL;
    str[bits] = 0;

    // type punning because signed shift is implementation-defined
    unsigned u = *(unsigned *)&i;
    for(; bits--; u >>= 1)
        str[bits] = u & 1 ? '1' : '0';

    return str;
}

static int interlogix_callback(bitbuffer_t *bitbuffer) {
    char time_str[LOCAL_TIME_BUFLEN];
    uint8_t *bb;
    data_t *data;
    unsigned int row = 0;
    char device_type[2];
    char *device_type_name = NULL;
    char device_serial[7];
    char device_raw_message[256];
    char *low_battery = NULL;
    char *f1_latch_state = NULL;
    char *f2_latch_state = NULL;
    char *f3_latch_state = NULL;
    char *f4_latch_state = NULL;
    char *f5_latch_state = NULL;

    if (debug_output >= 1) {
        fprintf(stderr,"GE/Interlogix Wireless Devices Template Callback\n");
    }
    if (debug_output >= 2 ) {

        fprintf(stderr, "bitbuffer:: Number of rows: %d \n", bitbuffer->num_rows);
        fprintf(stderr, "bitbuffer:: Length of 1st row: %d \n", bitbuffer->bits_per_row[row]);
        fprintf(stderr, "bitbuffer:: Printing 1st row: \n");

        //this is typically done in the bitbuffer_print function but depends upon length so doing it explicitly here
        for (unsigned int bit = 0; bit < bitbuffer->bits_per_row[row]; ++bit) {
            if (bitbuffer->bb[row][bit/8] & (0x80 >> (bit % 8))) {
                fprintf(stderr, "1");
            } else {
                fprintf(stderr, "0");
            }
        }
        fprintf(stderr, "\n");
        if (debug_output >=3)
            //print all rows
            bitbuffer_print(bitbuffer);
    }

    unsigned int bit_offset = bitbuffer_search(bitbuffer, row, 0, preamble, sizeof(preamble)*8);

    if (bitbuffer->bits_per_row[row] - bit_offset < 45) { //message should be at least 45 bits not including preamble bits
        if (debug_output >= 1)
            fprintf(stderr, "Found valid preamble but message size too small, exiting! \n");
        return 0;
    }
    else {
        //set message starting postion (just past preamble and sync bit)
        bit_offset = sizeof(preamble)*8 + bit_offset;

        //even parity check (for bits 15-54) against even parity bit at bit 55
        unsigned int calcParity = 0;
        unsigned int msgParity = 0;
        for (unsigned int bitc = bit_offset; bitc < bitbuffer->bits_per_row[row]; bitc++ )
        {
            if (bitc == (bit_offset+40)) { //15 bits for preamble and sync so bit 41 is actually documented bit 55
                msgParity ^= (bitbuffer->bb[row][bitc/8] & (0x80 >> (bitc % 8))) ? 1 : 0;
                if (calcParity == msgParity) {
                    //exit loop and attemt to decode message as we have a valid parity check
                    break;
                }
                else {
                    if (debug_output >=1)
                        fprintf(stderr, "Parity Check Failed! msgParity: %d and calcluated parity: %d \n", msgParity, calcParity);
                    //dont attempt decode if parity check failed
                    return 0;
                }
            }
            calcParity ^= (bitbuffer->bb[row][bitc/8] & (0x80 >> (bitc % 8))) ? 1 : 0;
        }

        uint8_t message[(INTERLOGIX_MSG_BIT_LEN/8)];

        bitbuffer_extract_bytes(bitbuffer, row, bit_offset, message, INTERLOGIX_MSG_BIT_LEN);

        if (debug_output >= 1) {
            //grab 6, 4 bit nibbles from bit buffer. need to reverse them to get serial number
            fprintf(stderr, "Device Serial Number: ");
            fprintf(stderr, "%02x", reverse8(message[2]));
            fprintf(stderr, "%02x", reverse8(message[1]));
            fprintf(stderr, "%02x", reverse8(message[0]));
            fprintf(stderr, "\n");

            fprintf(stderr, "Device Message: ");
            printBits(sizeof(message[0]), &message[0]);
            printBits(sizeof(message[1]), &message[1]);
            printBits(sizeof(message[2]), &message[2]);
            printBits(sizeof(message[3]), &message[3]);
            printBits(sizeof(message[4]), &message[4]);
            printBits(sizeof(message[5]), &message[5]);
            fprintf(stderr, "\n");
        }

        sprintf(device_type, "%01x", (reverse8(message[2])>>4));

        //TODO: look at conditionally parsing message for specific device types - raw message
        //      will be passed so parsing for specific device can be done at higher layer
        switch((reverse8(message[2])>>4)) {
            case 0xa: device_type_name = "contact sensor"; break;
            case 0xf: device_type_name = "keyfob"; break;
            case 0x4: device_type_name = "motion sensor"; break;
            case 0x6: device_type_name = "heat sensor"; break;

            default: device_type_name = "unknown"; break;
        }

        low_battery = (message[3] & 0x10) ? "on" : "off";
        sprintf(device_serial, "%02x%02x%02x", reverse8(message[2]), reverse8(message[1]), reverse8(message[0]));
        //TODO: find a better way to print in binary
        //sprintf(device_raw_message, "%s%s%s", int2bin(message[3]), int2bin(message[4]), int2bin(message[5]));
        sprintf(device_raw_message, "%x%x%x", message[3], message[4], message[5]);

        f1_latch_state = (message[3] & 0x04) ? "open" : "close";
        f2_latch_state = (message[3] & 0x01) ? "open" : "close";
        f3_latch_state = (message[4] & 0x40) ? "open" : "close";
        f4_latch_state = (message[4] & 0x10) ? "open" : "close";
        f5_latch_state = (message[4] & 0x04) ? "open" : "close";

        if (debug_output >= 1) {
            //10110110 00001000 11100101 TTT0DA01 DC01DE00 00110000
            fprintf(stderr, message[3] & 0x10 ? "Low Battery " : "Battery OK "); //tested
            fprintf(stderr, message[3] & 0x04 ? "F1 Latch OPEN " : "F1 Latch: CLOSED "); //tested
            fprintf(stderr, message[3] & 0x01 ? "F2 Latch OPEN " : "F2 Latch: CLOSED ");
            fprintf(stderr, message[4] & 0x40 ? "F3 Latch OPEN " : "F3 Latch: CLOSED "); //tested but noted associated debuce flag is debounced with f5 for some reason
            fprintf(stderr, message[4] & 0x10 ? "F4 Latch OPEN " : "F4 Latch: CLOSED ");
            fprintf(stderr, message[4] & 0x04 ? "F5 Positive Latch OPEN " : "F5 Positive Latch: CLOSED "); //tested but need to test with f5 negative results
            //fprintf(stderr, message[4] & 0x02 ? "F5 Negative Latch 1 " : "F5 Negative Latch: 0 ");

            fprintf(stderr, "\n");
        }

        local_time_str(0, time_str);

        data = data_make("time", "Receiver Time", DATA_STRING, time_str,
            "model", "Model", DATA_STRING, "GE Interlogix Device",
            "device_type", "Device Type", DATA_STRING, device_type,
            "device_type_name","Device Type Name", DATA_STRING, device_type_name,
            "device_serial","Device Serial Number", DATA_STRING, device_serial,
            "device_raw_message","Raw Message", DATA_STRING, device_raw_message,
            "low_battery","Low Battery Indicator", DATA_STRING, low_battery,
            "f1_latch_state","F1 Latch State", DATA_STRING, f1_latch_state,
            "f2_latch_state","F2 Latch State", DATA_STRING, f2_latch_state,
            "f3_latch_state","F3 Latch State", DATA_STRING, f3_latch_state,
            "f4_latch_state","F4 Latch State", DATA_STRING, f4_latch_state,
            "f5_latch_state","F5 Latch State", DATA_STRING, f5_latch_state,
            NULL);

        data_acquired_handler(data);

        // Return 1 if message successfully decoded
        return 1;
    }
}

/*
 * List of fields to output when using CSV
 *
 * Used to determine what fields will be output in what
 * order for this devince when using -F csv.
 *
 */
static char *csv_output_fields[] = {
    "time",
    "model",
    "device_type",
    "device_type_name",
    "device_serial",
    "low_battery",
    "device_raw_message",
    "f1_latch_state",
    "f2_latch_state",
    "f3_latch_state",
    "f4_latch_state",
    "f5_latch_state",
    NULL
};

/*
 * r_device - registers device/callback. see rtl_433_devices.h
 *
 * Timings:
 *
 * short, long, nad reset - specify pulse/period timings
 *     based on number of samples at 250 Khz samples/second.
 *     These timings will determine if the received pulses
 *     match, so your callback will fire after demodulation.
 *
 * for readabiliy, specify timings based on 1 Mhz samples
 *     but a divide by 4 in the definition.
 *
 *
 * Demodular:
 *
 * The function used to turn the received signal into bits.
 * See:
 * - pulse_demod.h for descriptions
 * - rtL_433.h for the list of defined names
 *
 */

r_device interlogix = {
    .name          = "Interlogix GE Security Device Decoder",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = (120+240)/2,
    .long_limit    = 360,
    .reset_limit   = 360,
    .json_callback = &interlogix_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = csv_output_fields,
};

