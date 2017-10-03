/* LaCrosse Color Forecast Station (model C85845), or other LaCrosse product
 * utilizing the remote temperature/humidity sensor TX141TH-Bv2 transmitting
 * in the 433.92 MHz band. Product pages:
 * http://www.lacrossetechnology.com/c85845-color-weather-station/
 * http://www.lacrossetechnology.com/tx141th-bv2-temperature-humidity-sensor
 *
 * The TX141TH-Bv2 protocol is OOK modulated PWM with fixed period of 625 us
 * for data bits, preambled by four long startbit pulses of fixed period equal
 * to ~1666 us. Hence, it is similar to Bresser Thermo-/Hygro-Sensor 3CH (bresser_3ch.c
 * included in this source code) with the exception that OOK_PULSE_PWM_TERNARY
 * modulation type is technically more correct than OOK_PULSE_PWM_RAW.
 *
 * A single data packet looks as follows:
 * 1) preamble - 833 us high followed by 833 us low, repeated 4 times:
 *  ----      ----      ----      ----
 * |    |    |    |    |    |    |    |
 *       ----      ----      ----      ----
 * 2) a train of 40 data pulses with fixed 625 us period follows immediately:
 *  ---    --     --     ---    ---    --     ---
 * |   |  |  |   |  |   |   |  |   |  |  |   |   |
 *      --    ---    ---     --     --    ---     -- ....
 * A logical 1 is 417 us of high followed by 208 us of low.
 * A logical 0 is 208 us of high followed by 417 us of low.
 * Thus, in the pictorial example above the bits are 1 0 0 1 1 0 1 ....
 *
 * The TX141TH-Bv2 sensor sends 12 of identical packets, one immediately following
 * the other, in a single burst. These 12-packet bursts repeat every 50 seconds. At
 * the end of the last packet there are two 833 us pulses ("post-amble"?).
 *
 * The data is grouped in 5 bytes / 10 nybbles
 * [id] [id] [flags] [temp] [temp] [temp] [humi] [humi] [chk] [chk]
 *
 * The "id" is an 8 bit random integer generated when the sensor powers up for the
 * first time; "flags" are 4 bits for battery low indicator, test button press,
 * and channel; "temp" is 12 bit unsigned integer which encodes temperature in degrees
 * Celsius as follows:
 * temp_c = temp/10 - 50
 * to account for the -40 C -- 60 C range; "humi" is 8 bit integer indicating
 * relative humidity in %. The method of calculating "chk", the presumed 8-bit checksum
 * remains a complete mystery at the moment of this writing, and I am not totally sure
 * if the last is any kind of CRC. I've run reveng 1.4.4 on exemplary data with all
 * available CRC algorithms and found no match. Be my guest if you want to
 * solve it - for example, if you figure out why the following two pairs have identical
 * checksums you'll become a hero:
 *
 * 0x87 0x02 0x3c 0x3b 0xe1
 * 0x87 0x02 0x7d 0x37 0xe1
 *
 * 0x87 0x01 0xc3 0x31 0xd8
 * 0x87 0x02 0x28 0x37 0xd8
 *
 * Developer's comment 1: because of our choice of the OOK_PULSE_PWM_TERNARY type, the input
 * array of bits will look like this:
 * bitbuffer:: Number of rows: 25
 *  [00] {0} :
 *  [01] {0} :
 *  [02] {0} :
 *  [03] {0} :
 *  [04] {40} 87 02 67 39 f6
 *  [05] {0} :
 *  [06] {0} :
 *  [07] {0} :
 *  [08] {40} 87 02 67 39 f6
 *  [09] {0} :
 *  [10] {0} :
 *  [11] {0} :
 *  [12] {40} 87 02 67 39 f6
 *  [13] {0} :
 *  [14] {0} :
 *  [15] {0} :
 *  [16] {40} 87 02 67 39 f6
 *  [17] {0} :
 *  [18] {0} :
 *  [19] {0} :
 *  [20] {40} 87 02 67 39 f6
 *  [21] {0} :
 *  [22] {0} :
 *  [23] {0} :
 *  [24] {280} 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6 87 02 67 39 f6
 * which is a direct consequence of two factors: (1) pulse_demod_pwm_ternary() always assuming
 * only one startbit, and (2) bitbuffer_add_row() not adding rows beyond BITBUF_ROWS. This is
 * OK because the data is clearly processable and the unique pattern minimizes the chance of
 * confusion with other sensors, particularly Bresser 3CH.
 *
 * Developer's comment 2: with unknown CRC (see above) the obvious way of checking the data
 * integrity is making use of the 12 packet repetition. In principle, transmission errors are
 * be relatively rare, thus the most frequent packet (statistical mode) should represent
 * the true data. Therefore, in the fisrt part of the callback routine the mode is determined
 * for the first 4 bytes of the data compressed into a single 32-bit integer. Since the packet
 * count is small, no sophisticated mode algorithm is necessary; a simple array of <data,count>
 * structures is sufficient. The added bonus is that relative count enables us to determine
 * the quality of radio transmission.
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

#define LACROSSE_TX141TH_BITLEN 40
#define LACROSSE_TX141TH_BYTELEN 5  // = LACROSSE_TX141TH_BITLEN / 8
#define LACROSSE_TX141TH_PACKETCOUNT 12

typedef struct {
    uint32_t data;  // First 4 data bytes compressed into 32-bit integer
    uint8_t count;  // Count
} data_and_count;

static int lacrosse_tx141th_bv2_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);
    int i,j,k,nbytes,npacket,kmax;
    uint8_t id=0,status=0,battery_low=0,test=0,humidity=0,maxcount;
    uint16_t temp_raw=0;
    float temp_f,temp_c=0.0;
    data_and_count dnc[LACROSSE_TX141TH_PACKETCOUNT] = {0};

    if (debug_output) {
        bitbuffer_print(bitbuffer);
    }

    npacket=0; // Number of unique packets
    for(i=0; i<BITBUF_ROWS; ++i) {
        j=bitbuffer->bits_per_row[i];
        if(j>=LACROSSE_TX141TH_BITLEN) {
            nbytes=j/8;
            for(j=0;j<nbytes;j+=LACROSSE_TX141TH_BYTELEN) {
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
                    if(npacket+1<LACROSSE_TX141TH_PACKETCOUNT) ++npacket;
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
    battery_low=(status & 0x80) >> 7;
    test=(status & 0x40) >> 6;
    temp_raw=((status & 0x0F) << 8) + bytes[2];
    temp_f = 9.0*((float)temp_raw)/50.0-58.0; // Temperature in F
    temp_c = ((float)temp_raw)/10.0-50.0; // Temperature in C
    humidity = bytes[3];

    if (0==id || 0==humidity || humidity > 100 || temp_f < -40.0 || temp_f > 140.0) {
        if (debug_output) {
            fprintf(stderr, "LaCrosse TX141TH-Bv2 data error\n");
            fprintf(stderr, "id: %i, humidity:%i, temp_f:%f\n", id, humidity, temp_f);
        }
        return 0;
    }

    data = data_make("time",    "Date and time", DATA_STRING,    time_str,
                     "model",   "", DATA_STRING,    "LaCrosse TX141TH-Bv2 sensor",
                     "id",      "Sensor ID",  DATA_FORMAT, "%02x", DATA_INT, id,
                     "temperature", "Temperature in deg F", DATA_FORMAT, "%.2f F", DATA_DOUBLE, temp_f,
                     "temperature_C", "Temperature in deg C", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                     "humidity",    "Humidity", DATA_FORMAT, "%u %%", DATA_INT, humidity,
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
    "humidity",
    "battery",
    "test",
    NULL
};

r_device lacrosse_TX141TH_Bv2 = {
    .name          = "LaCrosse TX141TH-Bv2 sensor",
    .modulation    = OOK_PULSE_PWM_TERNARY,
    .short_limit   = 312,     // short pulse is ~208 us, long pulse is ~417 us
    .long_limit    = 625,     // long gap (with short pulse) is ~417 us, sync gap is ~833 us
    .reset_limit   = 1500,   // maximum gap is 1250 us (long gap + longer sync gap on last repeat)
    .json_callback = &lacrosse_tx141th_bv2_callback,
    .disabled      = 0,
    .demod_arg     = 2,       // Longest pulses are startbits
    .fields        = output_fields,
};
