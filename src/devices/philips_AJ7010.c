/* 
 * Philips outdoor temperature sensor -- used with various Philips clock 
 * radios (tested on AJ7010).
 * This is inspired from the other Philips driver made by Chris Coffey.
 *
 * A complete message is 43 bits:
 *      4-bit initial preamble, always 0
 *      4-bit packet separator, always 0
 *      Packets are repeated 3 times.
 * 
 * 43-bit data packet format:
 *
 * 00001000 10100100 01000101 00101001 110 : g_philips_21.1_ch2_B.cu8
 * 00001011 01001111 10000101 00100001 111 : g_philips_21.4_ch1_C.cu8
 * 00001011 01000000 10100100 11001111 001 : gph_bootCh1_17.cu8
 * 00001000 10100011 11000100 11001111 101 : gph_bootCh2_17.cu8
 * 00000110 11011100 01100100 10111110 000 : gph_bootCh3_17.cu8
 *
 * Data format is:
 * 0000cccc ccc????? ???????ttt tttttttt ttt
 *
 * c - channel: 0x5A=channel 1, 0x45=channel 2, 0x36=channel 3 (7 bits)
 * t - temperature in ADC value that is then converted to deg. C. (14 bits)
 * s - CRC: non-standard CRC-?, poly 0x?, init 0x?
 *  
 * Copyright (C) 2018 Nicolas Jourden <nicolas.jourden@laposte.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtl_433.h"
#include "util.h"

#define PHILIPS_BITLEN       43
#define PHILIPS_DATALEN      5
#define PHILIPS_PACKETLEN    6
#define PHILIPS_STARTNIBBLE  0x0

// We have those values from the reading of the probe:
#define PHILIPS_MATH_COEF_XB      14576.0
#define PHILIPS_MATH_COEF_YB      36.8
#define PHILIPS_MATH_COEF_XA      1440.0
#define PHILIPS_MATH_COEF_YA      -14.3
#define PHILIPS_MATH_OFFSET      -20.0
// TODO: look for more efficient method:
#define PHILIPS_MATH_EXPR(v1)    (( ( (PHILIPS_MATH_COEF_YB-PHILIPS_MATH_COEF_YA)/(PHILIPS_MATH_COEF_XB-PHILIPS_MATH_COEF_XA) ) * v1 ) + PHILIPS_MATH_OFFSET)

static int philips_AJ7010_callback(bitbuffer_t *bitbuffer) 
{
    char time_str[LOCAL_TIME_BUFLEN];
    uint8_t packet[PHILIPS_DATALEN];
    data_t *data;
    unsigned int i = 0;
    unsigned int j = 0;
    uint8_t c_crc = 0;
    uint8_t r_crc = 0;
    uint8_t channel = 0;
    float temperature = 0;
    uint32_t tmp = 0;

    // Get the time
    local_time_str(0, time_str);
    bitbuffer_invert(bitbuffer); // ~ on data.

    // Correct number of rows?
    if (bitbuffer->num_rows != 1) {
        if (debug_output) {
            fprintf(stderr, "%s %s: wrong number of rows (%d)\n", 
                    time_str, __func__, bitbuffer->num_rows);
        }
        return 0;
    }

    // Correct bit length?
    if (bitbuffer->bits_per_row[0] != PHILIPS_BITLEN) {
        if (debug_output) {
            fprintf(stderr, "%s %s: wrong number of bits (%d)\n", 
                    time_str, __func__, bitbuffer->bits_per_row[0]);
        }
        return 0;
    }

    // Copy data of the packet to work on:
    memcpy(packet, bitbuffer->bb[0]+1, PHILIPS_DATALEN);
    if (debug_output) {
      fprintf(stderr, "\n%s %s: packet = ", time_str, __func__);
      for (i = 0; i < PHILIPS_DATALEN; i++) {
        fprintf(stderr, "%02x ",packet[i]);
      }
    }

    // Correct start sequence?
    if ((packet[0] >> 4) != PHILIPS_STARTNIBBLE) {
        if (debug_output) {
            fprintf(stderr, "%s %s: wrong start nibble\n", time_str, __func__);
        }
        return 0;
    }

    // TODO: CRC check.
    // Correct CRC?
/*
    r_crc = ((packet[4] >> 5) & 0x07 ) | ( (packet[3] << 3) &0x08 );
    fprintf(stderr, "r_crc decoded is %02x\n", r_crc);
    packet[4] = 0x0;
    packet[3] &= 0xfe;

    // force it:
    for (j = 0; j < 256; j++) {
    for (i = 0; i < 256; i++) {
      c_crc = crc4(packet, PHILIPS_DATALEN, i, j); // Including the CRC nibble

      if (r_crc == c_crc) {
 //       fprintf(stderr, "r_crc == c_crc with i=%02x j=%02x\n", i, j);
      }
     // else {
       // fprintf(stderr, "r_crc found is %04x\n", c_crc);
      //}
    }
    }
*/

/*
    if (r_crc != c_crc) {
        if (debug_output) {
            fprintf(stderr, "%s %s: combined packet = ", time_str, __func__);
            for (i = 0; i < PHILIPS_DATALEN; i++) {
              fprintf(stderr, "%02x ", packet[i]);
            }

            fprintf(stderr, "%s %s: CRC failed, calculated %x\n",
                    time_str, __func__, c_crc);
        }
        return 0;
    }
*/

    // Channel
    tmp = (packet[0] & 0x0f);
    tmp <<= 3;
    tmp |= (packet[1] >> 5);
    switch ( tmp )
    {
      case 0x36:
         channel = 3;
         break;
      case 0x45:
         channel = 2;
         break;
      case 0x5A:
         channel = 1;
         break;
      default:
         channel = 0;
         break;
    }
    if (debug_output) {
      fprintf(stderr, "channel decoded is %d\n",channel);
    }

    // Temperature
    tmp = (packet[2]) & 0x07;
    tmp <<= 8;
    tmp |= packet[3];
    tmp <<= 3;
    tmp |= packet[4];
    temperature = PHILIPS_MATH_EXPR(tmp);
    if (debug_output) {
      fprintf(stderr, "\ntemperature: raw: %u\t%08X\tconverted: %.2f\n", tmp, tmp, temperature);
    }

    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "Philips outdoor temperature sensor (type AJ7010)",
                     "channel",       "Channel",     DATA_INT,    channel,
                     "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
                     NULL);

    data_acquired_handler(data);

    return 1;
}

static char *philips_AJ7010_output_fields[] = {
    "time",
    "model",
    "channel",
    "temperature_C",
    NULL
};

r_device philips_AJ7010 = {
    .name          = "Philips outdoor temperature sensor (type AJ7010)",
    .modulation    = OOK_PULSE_PWM_PRECISE,
    .short_limit   = 2000,
    .long_limit    = 6000,
    .reset_limit   = 30000,
    .json_callback = &philips_AJ7010_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = philips_AJ7010_output_fields,
};

