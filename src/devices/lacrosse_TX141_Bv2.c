/* LaCrosse temperature sensor TX141-Bv2 transmitting in the 
 * 433.92 MHz band. Product page:
 * https://www.lacrossetechnology.com/tx141-bv2-temperature-sensor/
 *
 * The TX141-BV2 is the temperature only version of the TX141TH-BV2 sensor.
 *
 * This file is a copy of lacrosse_TX141TH_Bv2.c, please refer to it for 
 * comments about design. 
 *
 * Changes:
 * - All references to TX141TH have been changed to TX141 removing TH.
 * - LACROSSE_TX141_BITLEN is 37 instead of 40. 
 * - The humidity variable has been removed.
 * - Battery check bit is inverse of TX141TH.
 *
 * The CRC Checksum is not checked. In trying to reverse engineer the
 * CRC, the first nibble can be checked by:
 *
 * a1 = (bytes[0]&0xF0) >> 4);
 * b1 = (bytes[1]&0x40) >> 4) - 1;
 * c1 = (bytes[2]&0xF0) >> 4);
 * n1 = (a1+a2+c3)&0x0F;
 *
 * The second nibble I could not figure out.
 *
 * Changes done by Andrew Rivett (va3ned@gmail.com). Copyright is 
 * retained by Robert Fraczkiewicz.
 *
 * Copyright (C) 2017 Robert Fraczkiewicz   (aromring@gmail.com)
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include "data.h"
#include "rtl_433.h"
#include "util.h"

#define LACROSSE_TX141_BITLEN 37
#define LACROSSE_TX141_BYTELEN 5  // = LACROSSE_TX141_BITLEN / 8
#define LACROSSE_TX141_PACKETCOUNT 12

typedef struct {
    uint32_t data;  // First 4 data bytes compressed into 32-bit integer
    uint8_t count;  // Count
} data_and_count;

static int lacrosse_tx141_bv2_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);
    int i,j,k,nbytes,npacket,kmax;
    uint8_t id=0,status=0,battery_low=0,test=0,maxcount;
    uint16_t temp_raw=0;
    float temp_f,temp_c=0.0;
    data_and_count dnc[LACROSSE_TX141_PACKETCOUNT] = {0};

    if (debug_output) {
        bitbuffer_print(bitbuffer);
    }

    npacket=0; // Number of unique packets
    for(i=0; i<BITBUF_ROWS; ++i) {
        j=bitbuffer->bits_per_row[i];
        if(j>=LACROSSE_TX141_BITLEN) {
            nbytes=j/8;
            for(j=0;j<nbytes;j+=LACROSSE_TX141_BYTELEN) {
                uint32_t *d=(uint32_t *)(bb[i]+j);
                uint8_t not_found=1;
                for(k=0;k<npacket;++k) {
                    if(*d==dnc[k].data) {
                        ++(dnc[k].count);
                        not_found=0;
                        break;
                    }
                }
                if(not_found) {
                    dnc[npacket].data=*d;
                    dnc[npacket].count=1;
                    if(npacket+1<LACROSSE_TX141_PACKETCOUNT) ++npacket;
                }
            }
        }
    }

    if (debug_output) {
        fprintf(stderr, "%d unique packet(s)\n", npacket);
        for(k=0;k<npacket;++k) {
            fprintf(stderr, "%08x \t %d \n", dnc[k].data,dnc[k].count);
        }
    }

    // Find the most frequent data packet, if necessary
    kmax=0;
    maxcount=0;
    if(npacket>1) {
        for(k=0;k<npacket;++k) {
            if(dnc[k].count>maxcount) {
                maxcount=dnc[k].count;
                kmax=k;
            }
        }
    }

    // reduce false positives, require at least 5 out of 12 repeats.
    if (dnc[kmax].count < 5) {
        return 0;
    }

    // Unpack the data bytes back to eliminate dependence on the platform endiannes!
    uint8_t *bytes=(uint8_t*)(&(dnc[kmax].data));
    id=bytes[0];
    status=bytes[1];
    battery_low=!((status & 0x80) >> 7);
    test=(status & 0x40) >> 6;
    temp_raw=((status & 0x0F) << 8) + bytes[2];
    temp_f = 9.0*((float)temp_raw)/50.0-58.0; // Temperature in F
    temp_c = ((float)temp_raw)/10.0-50.0; // Temperature in C

    if (0==id || temp_f < -40.0 || temp_f > 140.0) {
        if (debug_output) {
            fprintf(stderr, "LaCrosse TX141-Bv2 data error\n");
            fprintf(stderr, "id: %i, temp_f:%f\n", id, temp_f);
        }
        return 0;
    }

    data = data_make("time",    "Date and time", DATA_STRING,    time_str,
                     "model",   "", DATA_STRING,    "LaCrosse TX141-Bv2 sensor",
                     "id",      "Sensor ID",  DATA_FORMAT, "%02x", DATA_INT, id,
                     "temperature", "Temperature in deg F", DATA_FORMAT, "%.2f F", DATA_DOUBLE, temp_f,
                     "temperature_C", "Temperature in deg C", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                     "battery", "Battery",  DATA_STRING, battery_low ? "LOW" : "OK",
                     "test",    "Test?",  DATA_STRING, test ? "Yes" : "No",
                      NULL);
    data_acquired_handler(data);

    return 1;

}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "temperature",
    "temperature_C",
    "battery",
    "test",
    NULL
};

r_device lacrosse_TX141_Bv2 = {
    .name          = "LaCrosse TX141-Bv2 sensor",
    .modulation    = OOK_PULSE_PWM_TERNARY,
    .short_limit   = 312,     // short pulse is ~208 us, long pulse is ~417 us
    .long_limit    = 625,     // long gap (with short pulse) is ~417 us, sync gap is ~833 us
    .reset_limit   = 1500,   // maximum gap is 1250 us (long gap + longer sync gap on last repeat)
    .json_callback = &lacrosse_tx141_bv2_callback,
    .disabled      = 0,
    .demod_arg     = 2,       // Longest pulses are startbits
    .fields        = output_fields,
};
