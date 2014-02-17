/*
 * rtl_433, turns your Realtek RTL2832 based DVB dongle into a 433.92MHz generic data receiver
 * Copyright (C) 2012 by Benjamin Larsson <benjamin@southpole.se>
 *
 * Based on rtl_sdr
 *
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


/* Currently this can decode the temperature and id from Rubicson sensors
 *
 * the sensor sends 36 bits 12 times pwm modulated
 * the data is grouped into 9 nibles
 * [id0] [id1], [unk0] [temp0], [temp1] [temp2], [unk1] [unk2], [unk3]
 *
 * The id changes when the battery is changed in the sensor.
 * unk0 is always 1 0 0 0, most likely 2 channel bits as the sensor can recevice 3 channels
 * unk1-3 changes and the meaning is unknown
 * temp is 12 bit signed scaled by 10
 *
 * The sensor can be bought at Kjell&Co
 */

/* Prologue sensor protocol
 *
 * the sensor sends 36 bits 7 times, before the first packet there is a pulse sent
 * the packets are pwm modulated
 *
 * the data is grouped in 9 nibles
 * [id0] [rid0] [rid1] [data0] [temp0] [temp1] [temp2] [humi0] [humi1]
 *
 * id0 is always 1001,9
 * rid is a random id that is generated when the sensor starts, could include battery status
 * the same batteries often generate the same id
 * data(3) is 0 the battery status, 1 ok, 0 low, first reading always say low
 * data(2) is 1 when the sensor sends a reading when pressing the button on the sensor
 * data(1,0)+1 forms the channel number that can be set by the sensor (1-3)
 * temp is 12 bit signed scaled by 10
 * humi0 is always 1100,c if no humidity sensor is available
 * humi1 is always 1100,c if no humidity sensor is available
 *
 * The sensor can be bought at Clas Ohlson
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#include "getopt/getopt.h"
#endif

#include "rtl-sdr.h"

#define DEFAULT_SAMPLE_RATE     250000
#define DEFAULT_FREQUENCY       433920000
#define DEFAULT_HOP_TIME        (60*10)
#define DEFAULT_HOP_EVENTS      2
#define DEFAULT_ASYNC_BUF_NUMBER    32
#define DEFAULT_BUF_LENGTH      (16 * 16384)
#define DEFAULT_LEVEL_LIMIT     10000
#define DEFAULT_DECIMATION_LEVEL 0
#define MINIMAL_BUF_LENGTH      512
#define MAXIMAL_BUF_LENGTH      (256 * 16384)
#define FILTER_ORDER            1
#define MAX_PROTOCOLS           10
#define SIGNAL_GRABBER_BUFFER   (12 * DEFAULT_BUF_LENGTH)
#define BITBUF_COLS             34
#define BITBUF_ROWS             50

static int do_exit = 0;
static int do_exit_async=0, frequencies=0, events=0;
uint32_t frequency[MAX_PROTOCOLS];
time_t rawtime_old;
int flag;
uint32_t samp_rate=DEFAULT_SAMPLE_RATE;
static uint32_t bytes_to_read = 0;
static rtlsdr_dev_t *dev = NULL;
static uint16_t scaled_squares[256];
static int debug_output = 0;
static int override_short = 0;
static int override_long = 0;

/* Supported modulation types */
#define     OOK_PWM_D   1   /* Pulses are of the same length, the distance varies */
#define     OOK_PWM_P   2   /* The length of the pulses varies */


typedef struct {
    unsigned int    id;
    char            name[256];
    unsigned int    modulation;
    unsigned int    short_limit;
    unsigned int    long_limit;
    unsigned int    reset_limit;
    int     (*json_callback)(uint8_t bits_buffer[BITBUF_ROWS][BITBUF_COLS]) ;
} r_device;

static int debug_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS]) {
    int i,j,k;
    fprintf(stderr, "\n");
    for (i=0 ; i<BITBUF_ROWS ; i++) {
        fprintf(stderr, "[%02d] ",i);
        for (j=0 ; j<BITBUF_COLS ; j++) {
            fprintf(stderr, "%02x ", bb[i][j]);
        }
        fprintf(stderr, ": ");
        for (j=0 ; j<BITBUF_COLS ; j++) {
            for (k=7 ; k>=0 ; k--) {
                if (bb[i][j] & 1<<k)
                    fprintf(stderr, "1");
                else
                    fprintf(stderr, "0");
            }
            fprintf(stderr, " ");
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");

    return 0;
}

static int silvercrest_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS]) {
    /* FIXME validate the received message better */
    if (bb[1][0] == 0xF8 &&
        bb[2][0] == 0xF8 &&
        bb[3][0] == 0xF8 &&
        bb[4][0] == 0xF8 &&
        bb[1][1] == 0x4d &&
        bb[2][1] == 0x4d &&
        bb[3][1] == 0x4d &&
        bb[4][1] == 0x4d) {
        /* Pretty sure this is a Silvercrest remote */
        fprintf(stderr, "Remote button event:\n");
        fprintf(stderr, "model = Silvercrest\n");
        fprintf(stderr, "%02x %02x %02x %02x %02x\n",bb[1][0],bb[0][1],bb[0][2],bb[0][3],bb[0][4]);

        if (debug_output)
            debug_callback(bb);

        return 1;
    }
    return 0;
}

static int rubicson_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS]) {
    int temperature_before_dec;
    int temperature_after_dec;
    int16_t temp;

    /* FIXME validate the received message better, figure out crc */
    if (bb[1][0] == bb[2][0] && bb[2][0] == bb[3][0] && bb[3][0] == bb[4][0] &&
        bb[4][0] == bb[5][0] && bb[5][0] == bb[6][0] && bb[6][0] == bb[7][0] && bb[7][0] == bb[8][0] &&
        bb[8][0] == bb[9][0] && (bb[5][0] != 0 && bb[5][1] != 0 && bb[5][2] != 0)) {

        /* Nible 3,4,5 contains 12 bits of temperature
         * The temerature is signed and scaled by 10 */
        temp = (int16_t)((uint16_t)(bb[0][1] << 12) | (bb[0][2] << 4));
        temp = temp >> 4;

        temperature_before_dec = abs(temp / 10);
        temperature_after_dec = abs(temp % 10);

        fprintf(stderr, "Sensor temperature event:\n");
        fprintf(stderr, "protocol       = Rubicson/Auriol\n");
        fprintf(stderr, "rid            = %x\n",bb[0][0]);
        fprintf(stderr, "temp           = %s%d.%d\n",temp<0?"-":"",temperature_before_dec, temperature_after_dec);
        fprintf(stderr, "%02x %02x %02x %02x %02x\n",bb[1][0],bb[0][1],bb[0][2],bb[0][3],bb[0][4]);

        if (debug_output)
            debug_callback(bb);

        return 1;
    }
    return 0;
}

static int prologue_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS]) {
    int rid;

    int16_t temp2;

    /* FIXME validate the received message better */
    if (((bb[1][0]&0xF0) == 0x90 && (bb[2][0]&0xF0) == 0x90 && (bb[3][0]&0xF0) == 0x90 && (bb[4][0]&0xF0) == 0x90 &&
        (bb[5][0]&0xF0) == 0x90 && (bb[6][0]&0xF0) == 0x90) ||
        ((bb[1][0]&0xF0) == 0x50 && (bb[2][0]&0xF0) == 0x50 && (bb[3][0]&0xF0) == 0x50 && (bb[4][0]&0xF0) == 0x50) &&
        (bb[1][3] == bb[2][3]) && (bb[1][4] == bb[2][4])) {

        /* Prologue sensor */
        temp2 = (int16_t)((uint16_t)(bb[1][2] << 8) | (bb[1][3]&0xF0));
        temp2 = temp2 >> 4;
        fprintf(stderr, "Sensor temperature event:\n");
        fprintf(stderr, "protocol      = Prologue\n");
        fprintf(stderr, "button        = %d\n",bb[1][1]&0x04?1:0);
        fprintf(stderr, "battery       = %s\n",bb[1][1]&0x08?"Ok":"Low");
        fprintf(stderr, "temp          = %s%d.%d\n",temp2<0?"-":"",abs((int16_t)temp2/10),abs((int16_t)temp2%10));
        fprintf(stderr, "humidity      = %d\n", ((bb[1][3]&0x0F)<<4)|(bb[1][4]>>4));
        fprintf(stderr, "channel       = %d\n",(bb[1][1]&0x03)+1);
        fprintf(stderr, "id            = %d\n",(bb[1][0]&0xF0)>>4);
        rid = ((bb[1][0]&0x0F)<<4)|(bb[1][1]&0xF0)>>4;
        fprintf(stderr, "rid           = %d\n", rid);
        fprintf(stderr, "hrid          = %02x\n", rid);

        fprintf(stderr, "%02x %02x %02x %02x %02x\n",bb[1][0],bb[1][1],bb[1][2],bb[1][3],bb[1][4]);

        if (debug_output)
            debug_callback(bb);

        return 1;
    }
    return 0;
}

static int waveman_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS]) {
    /* Two bits map to 2 states, 0 1 -> 0 and 1 1 -> 1 */
    int i;
    uint8_t nb[3] = {0};

    if (((bb[0][0]&0x55)==0x55) && ((bb[0][1]&0x55)==0x55) && ((bb[0][2]&0x55)==0x55) && ((bb[0][3]&0x55)==0x00)) {
        for (i=0 ; i<3 ; i++) {
            nb[i] |= ((bb[0][i]&0xC0)==0xC0) ? 0x00 : 0x01;
            nb[i] |= ((bb[0][i]&0x30)==0x30) ? 0x00 : 0x02;
            nb[i] |= ((bb[0][i]&0x0C)==0x0C) ? 0x00 : 0x04;
            nb[i] |= ((bb[0][i]&0x03)==0x03) ? 0x00 : 0x08;
        }

        fprintf(stderr, "Remote button event:\n");
        fprintf(stderr, "model   = Waveman Switch Transmitter\n");
        fprintf(stderr, "id      = %c\n", 'A'+nb[0]);
        fprintf(stderr, "channel = %d\n", (nb[1]>>2)+1);
        fprintf(stderr, "button  = %d\n", (nb[1]&3)+1);
        fprintf(stderr, "state   = %s\n", (nb[2]==0xe) ? "on" : "off");
        fprintf(stderr, "%02x %02x %02x\n",nb[0],nb[1],nb[2]);

        if (debug_output)
            debug_callback(bb);

        return 1;
    }
    return 0;
}


uint16_t AD_POP(uint8_t bb[BITBUF_COLS], uint8_t bits, uint8_t bit) {
    uint16_t val = 0;
    uint8_t i, byte_no, bit_no;
    for (i=0;i<bits;i++) {
        byte_no=   (bit+i)/8 ;
        bit_no =7-((bit+i)%8);
        if (bb[byte_no]&(1<<bit_no)) val = val | (1<<i);
    }
    return val;
}

static int em1000_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS]) {
    // based on fs20.c
    uint8_t dec[10];
    uint8_t bytes=0;
    uint8_t bit=18; // preamble
    uint8_t bb_p[14];
    char* types[] = {"S", "?", "GZ"};
    uint8_t checksum_calculated = 0;
    uint8_t i;
	uint8_t stopbit;
	uint8_t checksum_received;

    // check and combine the 3 repetitions
    for (i = 0; i < 14; i++) {
        if(bb[0][i]==bb[1][i] || bb[0][i]==bb[2][i]) bb_p[i]=bb[0][i];
        else if(bb[1][i]==bb[2][i])                  bb_p[i]=bb[1][i];
        else return 0;
    }

    // read 9 bytes with stopbit ...
    for (i = 0; i < 9; i++) {
        dec[i] = AD_POP (bb_p, 8, bit); bit+=8;
        stopbit=AD_POP (bb_p, 1, bit); bit+=1;
        if (!stopbit) {
//            fprintf(stderr, "!stopbit: %i\n", i);
            return 0;
        }
        checksum_calculated ^= dec[i];
        bytes++;
    }

    // Read checksum
    checksum_received = AD_POP (bb_p, 8, bit); bit+=8;
    if (checksum_received != checksum_calculated) {
//        fprintf(stderr, "checksum_received != checksum_calculated: %d %d\n", checksum_received, checksum_calculated);
        return 0;
    }

//for (i = 0; i < bytes; i++) fprintf(stderr, "%02X ", dec[i]); fprintf(stderr, "\n");

    // based on 15_CUL_EM.pm
    fprintf(stderr, "Energy sensor event:\n");
    fprintf(stderr, "protocol      = ELV EM 1000\n");
    fprintf(stderr, "type          = EM 1000-%s\n",dec[0]>=1&&dec[0]<=3?types[dec[0]-1]:"?");
    fprintf(stderr, "code          = %d\n",dec[1]);
    fprintf(stderr, "seqno         = %d\n",dec[2]);
    fprintf(stderr, "total cnt     = %d\n",dec[3]|dec[4]<<8);
    fprintf(stderr, "current cnt   = %d\n",dec[5]|dec[6]<<8);
    fprintf(stderr, "peak cnt      = %d\n",dec[7]|dec[8]<<8);

    return 1;
}

static int ws2000_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS]) {
    // based on http://www.dc3yc.privat.t-online.de/protocol.htm
    uint8_t dec[13];
    uint8_t nibbles=0;
    uint8_t bit=11; // preamble
    char* types[]={"!AS3", "AS2000/ASH2000/S2000/S2001A/S2001IA/ASH2200/S300IA", "!S2000R", "!S2000W", "S2001I/S2001ID", "!S2500H", "!Pyrano", "!KS200/KS300"};
    uint8_t check_calculated=0, sum_calculated=0;
    uint8_t i;
    uint8_t stopbit;
	uint8_t sum_received;

    dec[0] = AD_POP (bb[0], 4, bit); bit+=4;
    stopbit= AD_POP (bb[0], 1, bit); bit+=1;
    if (!stopbit) {
//fprintf(stderr, "!stopbit\n");
        return 0;
    }
    check_calculated ^= dec[0];
    sum_calculated   += dec[0];

    // read nibbles with stopbit ...
    for (i = 1; i <= (dec[0]==4?12:8); i++) {
        dec[i] = AD_POP (bb[0], 4, bit); bit+=4;
        stopbit= AD_POP (bb[0], 1, bit); bit+=1;
        if (!stopbit) {
//fprintf(stderr, "!stopbit %i\n", i);
            return 0;
        }
        check_calculated ^= dec[i];
        sum_calculated   += dec[i];
        nibbles++;
    }

    if (check_calculated) {
//fprintf(stderr, "check_calculated (%d) != 0\n", check_calculated);
        return 0;
    }

    // Read sum
    sum_received = AD_POP (bb[0], 4, bit); bit+=4;
    sum_calculated+=5;
    sum_calculated&=0xF;
    if (sum_received != sum_calculated) {
//fprintf(stderr, "sum_received (%d) != sum_calculated (%d) ", sum_received, sum_calculated);
        return 0;
    }

//for (i = 0; i < nibbles; i++) fprintf(stderr, "%02X ", dec[i]); fprintf(stderr, "\n");

    fprintf(stderr, "Weather station sensor event:\n");
    fprintf(stderr, "protocol      = ELV WS 2000\n");
    fprintf(stderr, "type (!=ToDo) = %s\n", dec[0]<=7?types[dec[0]]:"?");
    fprintf(stderr, "code          = %d\n", dec[1]&7);
    fprintf(stderr, "temp          = %s%d.%d\n", dec[1]&8?"-":"", dec[4]*10+dec[3], dec[2]);
    fprintf(stderr, "humidity      = %d.%d\n", dec[7]*10+dec[6], dec[5]);
    if(dec[0]==4) {
        fprintf(stderr, "pressure      = %d\n", 200+dec[10]*100+dec[9]*10+dec[8]);
    }

    return 1;
}


// timings based on samp_rate=1024000
r_device rubicson = {
    /* .id             = */ 1,
    /* .name           = */ "Rubicson Temperature Sensor",
    /* .modulation     = */ OOK_PWM_D,
    /* .short_limit    = */ 1744/4,
    /* .long_limit     = */ 3500/4,
    /* .reset_limit    = */ 5000/4,
    /* .json_callback  = */ &rubicson_callback,
};

r_device prologue = {
    /* .id             = */ 2,
    /* .name           = */ "Prologue Temperature Sensor",
    /* .modulation     = */ OOK_PWM_D,
    /* .short_limit    = */ 3500/4,
    /* .long_limit     = */ 7000/4,
    /* .reset_limit    = */ 15000/4,
    /* .json_callback  = */ &prologue_callback,
};

r_device silvercrest = {
    /* .id             = */ 3,
    /* .name           = */ "Silvercrest Remote Control",
    /* .modulation     = */ OOK_PWM_P,
    /* .short_limit    = */ 600/4,
    /* .long_limit     = */ 5000/4,
    /* .reset_limit    = */ 15000/4,
    /* .json_callback  = */ &silvercrest_callback,
};

r_device tech_line_fws_500 = {
    /* .id             = */ 4,
    /* .name           = */ "Tech Line FWS-500 Sensor",
    /* .modulation     = */ OOK_PWM_D,
    /* .short_limit    = */ 3500/4,
    /* .long_limit     = */ 7000/4,
    /* .reset_limit    = */ 15000/4,
    // /* .json_callback  = */ &rubicson_callback,
};

r_device generic_hx2262 = {
    /* .id             = */ 5,
    /* .name           = */ "Window/Door sensor",
    /* .modulation     = */ OOK_PWM_P,
    /* .short_limit    = */ 1300/4,
    /* .long_limit     = */ 10000/4,
    /* .reset_limit    = */ 40000/4,
    // /* .json_callback  = */ &silvercrest_callback,
};

r_device technoline_ws9118 = {
    /* .id             = */ 6,
    /* .name           = */ "Technoline WS9118",
    /* .modulation     = */ OOK_PWM_D,
    /* .short_limit    = */ 1800/4,
    /* .long_limit     = */ 3500/4,
    /* .reset_limit    = */ 15000/4,
    /* .json_callback  = */ &debug_callback,
};


r_device elv_em1000 = {
    /*.id             = */ 7,
    /*.name           = */ "ELV EM 1000",
    /*.modulation     = */ OOK_PWM_D,
    /*.short_limit    = */ 750/4,
    /* .long_limit     = */ 7250/4,
    /* .reset_limit    = */ 30000/4,
    /* .json_callback  = */ &em1000_callback,
};

r_device elv_ws2000 = {
    /*.id             = */ 8,
    /*.name           = */ "ELV WS 2000",
    /*.modulation     = */ OOK_PWM_D,
    /*.short_limit    = */ (602+(1155-602)/2)/4,
    /* .long_limit     = */ ((1755635-1655517)/2)/4, // no repetitions
    /* .reset_limit    = */ ((1755635-1655517)*2)/4,
    /* .json_callback  = */ &ws2000_callback,
};

r_device waveman = {
    /*.id             = */ 6,
    /*.name           = */ "Waveman Switch Transmitter",
    /*.modulation     = */ OOK_PWM_P,
    /*.short_limit    = */ 1000/4,
    /* .long_limit     = */ 8000/4,
    /* .reset_limit    = */ 30000/4,
    /* .json_callback  = */ &waveman_callback,
};


struct protocol_state {
    int (*callback)(uint8_t bits_buffer[BITBUF_ROWS][BITBUF_COLS]);

    /* bits state */
    int bits_col_idx;
    int bits_row_idx;
    int bits_bit_col_idx;
    uint8_t bits_buffer[BITBUF_ROWS][BITBUF_COLS];
    int16_t bits_per_row[BITBUF_ROWS];
    int     bit_rows;
    unsigned int modulation;

    /* demod state */
    int pulse_length;
    int pulse_count;
    int pulse_distance;
    int sample_counter;
    int start_c;

    int packet_present;
    int pulse_start;
    int real_bits;
    int start_bit;
    /* pwm limits */
    int short_limit;
    int long_limit;
    int reset_limit;


};


struct dm_state {
    FILE *file;
    int save_data;
    int32_t level_limit;
    int32_t decimation_level;
    int16_t filter_buffer[MAXIMAL_BUF_LENGTH+FILTER_ORDER];
    int16_t* f_buf;
    int analyze;
    int debug_mode;

    /* Signal grabber variables */
    int signal_grabber;
    int8_t* sg_buf;
    int sg_index;
    int sg_len;


    /* Protocol states */
    int r_dev_num;
    struct protocol_state *r_devs[MAX_PROTOCOLS];

};

void usage(void)
{
    fprintf(stderr,
        "rtl_433, an ISM band generic data receiver for RTL2832 based DVB-T receivers\n\n"
        "Usage:\t[-d device_index (default: 0)]\n"
        "\t[-g gain (default: 0 for auto)]\n"
        "\t[-a analyze mode, print a textual description of the signal]\n"
        "\t[-t signal auto save, use it together with analyze mode (-a -t)\n"
        "\t[-l change the detection level used to determine pulses (0-3200) default: %i]\n"
        "\t[-f [-f...] receive frequency[s], default: %i Hz]\n"
        "\t[-s samplerate (default: %i Hz)]\n"
        "\t[-S force sync output (default: async)]\n"
        "\t[-r read data from file instead of from a receiver]\n"
        "\t[-p ppm_error (default: 0)]\n"
        "\t[-r test file name (indata)]\n"
        "\t[-m test file mode (0 rtl_sdr data, 1 rtl_433 data)]\n"
        "\t[-D print debug info on event\n"
        "\t[-z override short value\n"
        "\t[-x override long value\n"
        "\tfilename (a '-' dumps samples to stdout)\n\n", DEFAULT_LEVEL_LIMIT, DEFAULT_FREQUENCY, DEFAULT_SAMPLE_RATE);
    exit(1);
}

#ifdef _WIN32
BOOL WINAPI
sighandler(int signum)
{
    if (CTRL_C_EVENT == signum) {
        fprintf(stderr, "Signal caught, exiting!\n");
        do_exit = 1;
        rtlsdr_cancel_async(dev);
        return TRUE;
    }
    return FALSE;
}
#else
static void sighandler(int signum)
{
    fprintf(stderr, "Signal caught, exiting!\n");
    do_exit = 1;
    rtlsdr_cancel_async(dev);
}
#endif

/* precalculate lookup table for envelope detection */
static void calc_squares() {
    int i;
    for (i=0 ; i<256 ; i++)
        scaled_squares[i] = (128-i) * (128-i);
}

/** This will give a noisy envelope of OOK/ASK signals
 *  Subtract the bias (-128) and get an envelope estimation
 *  The output will be written in the input buffer
 *  @returns   pointer to the input buffer
 */

static void envelope_detect(unsigned char *buf, uint32_t len, int decimate)
{
    uint16_t* sample_buffer = (uint16_t*) buf;
    unsigned int i;
    unsigned op = 0;
    unsigned int stride = 1<<decimate;

    for (i=0 ; i<len/2 ; i+=stride) {
        sample_buffer[op++] = scaled_squares[buf[2*i  ]]+scaled_squares[buf[2*i+1]];
    }
}

static void demod_reset_bits_packet(struct protocol_state* p) {
    memset(p->bits_buffer, 0 ,BITBUF_ROWS*BITBUF_COLS);
    memset(p->bits_per_row, 0 ,BITBUF_ROWS);
    p->bits_col_idx = 0;
    p->bits_bit_col_idx = 7;
    p->bits_row_idx = 0;
    p->bit_rows = 0;
}

static void demod_add_bit(struct protocol_state* p, int bit) {
    p->bits_buffer[p->bits_row_idx][p->bits_col_idx] |= bit<<p->bits_bit_col_idx;
    p->bits_bit_col_idx--;
    if (p->bits_bit_col_idx<0) {
        p->bits_bit_col_idx = 7;
        p->bits_col_idx++;
        if (p->bits_col_idx>BITBUF_COLS-1) {
            p->bits_col_idx = BITBUF_COLS-1;
//            fprintf(stderr, "p->bits_col_idx>%i!\n", BITBUF_COLS-1);
        }
    }
    p->bits_per_row[p->bit_rows]++;
}

static void demod_next_bits_packet(struct protocol_state* p) {
    p->bits_col_idx = 0;
    p->bits_row_idx++;
    p->bits_bit_col_idx = 7;
    if (p->bits_row_idx>BITBUF_ROWS-1) {
        p->bits_row_idx = BITBUF_ROWS-1;
        //fprintf(stderr, "p->bits_row_idx>%i!\n", BITBUF_ROWS-1);
    }
    p->bit_rows++;
    if (p->bit_rows > BITBUF_ROWS-1)
        p->bit_rows -=1;
}

static void demod_print_bits_packet(struct protocol_state* p) {
    int i,j,k;

    fprintf(stderr, "\n");
    for (i=0 ; i<p->bit_rows+1 ; i++) {
        fprintf(stderr, "[%02d] {%d} ",i, p->bits_per_row[i]);
        for (j=0 ; j<((p->bits_per_row[i]+8)/8) ; j++) {
	        fprintf(stderr, "%02x ", p->bits_buffer[i][j]);
        }
        fprintf(stderr, ": ");
        for (j=0 ; j<((p->bits_per_row[i]+8)/8) ; j++) {
            for (k=7 ; k>=0 ; k--) {
                if (p->bits_buffer[i][j] & 1<<k)
                    fprintf(stderr, "1");
                else
                    fprintf(stderr, "0");
            }
//            fprintf(stderr, "=0x%x ",demod->bits_buffer[i][j]);
            fprintf(stderr, " ");
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "\n");
    return;
}

static void register_protocol(struct dm_state *demod, r_device *t_dev) {
    struct protocol_state *p =  calloc(1,sizeof(struct protocol_state));
    p->short_limit  = (float)t_dev->short_limit/((float)DEFAULT_SAMPLE_RATE/(float)samp_rate);
    p->long_limit   = (float)t_dev->long_limit /((float)DEFAULT_SAMPLE_RATE/(float)samp_rate);
    p->reset_limit  = (float)t_dev->reset_limit/((float)DEFAULT_SAMPLE_RATE/(float)samp_rate);
    p->modulation   = t_dev->modulation;
    p->callback     = t_dev->json_callback;
    demod_reset_bits_packet(p);

    demod->r_devs[demod->r_dev_num] = p;
    demod->r_dev_num++;

    fprintf(stderr, "Registering protocol[%02d] %s\n",demod->r_dev_num, t_dev->name);

    if (demod->r_dev_num > MAX_PROTOCOLS)
        fprintf(stderr, "Max number of protocols reached %d\n",MAX_PROTOCOLS);
}


static unsigned int counter = 0;
static unsigned int print = 1;
static unsigned int print2 = 0;
static unsigned int pulses_found = 0;
static unsigned int prev_pulse_start = 0;
static unsigned int pulse_start = 0;
static unsigned int pulse_end = 0;
static unsigned int pulse_avg = 0;
static unsigned int signal_start = 0;
static unsigned int signal_end   = 0;
static unsigned int signal_pulse_data[4000][3] = {{0}};
static unsigned int signal_pulse_counter = 0;


static void classify_signal() {
    unsigned int i,k, max=0, min=1000000, t;
    unsigned int delta, count_min, count_max, min_new, max_new, p_limit;
    unsigned int a[3], b[2], a_cnt[3], a_new[3], b_new[2];
    unsigned int signal_distance_data[4000] = {0};
    struct protocol_state p = {0};
    unsigned int signal_type;

    if (!signal_pulse_data[0][0])
        return;

    for (i=0 ; i<1000 ; i++) {
        if (signal_pulse_data[i][0] > 0) {
            //fprintf(stderr, "[%03d] s: %d\t  e:\t %d\t l:%d\n",
            //i, signal_pulse_data[i][0], signal_pulse_data[i][1],
            //signal_pulse_data[i][2]);
            if (signal_pulse_data[i][2] > max)
                max = signal_pulse_data[i][2];
            if (signal_pulse_data[i][2] <= min)
                min = signal_pulse_data[i][2];
        }
    }
    t=(max+min)/2;
    //fprintf(stderr, "\n\nMax: %d, Min: %d  t:%d\n", max, min, t);

    delta = (max - min)*(max-min);

    //TODO use Lloyd-Max quantizer instead
    k=1;
    while((k < 10) && (delta > 0)) {
        min_new = 0; count_min = 0;
        max_new = 0; count_max = 0;

        for (i=0 ; i < 1000 ; i++) {
            if (signal_pulse_data[i][0] > 0) {
                if (signal_pulse_data[i][2] < t) {
                    min_new = min_new + signal_pulse_data[i][2];
                    count_min++;
                }
                else {
                    max_new = max_new + signal_pulse_data[i][2];
                    count_max++;
                }
            }
        }
        min_new = min_new / count_min;
        max_new = max_new / count_max;

        delta = (min - min_new)*(min - min_new) + (max - max_new)*(max - max_new);
        min = min_new;
        max = max_new;
        t = (min + max)/2;

        fprintf(stderr, "Iteration %d. t: %d    min: %d (%d)    max: %d (%d)    delta %d\n", k,t, min, count_min, max, count_max, delta);
        k++;
    }

    for (i=0 ; i<1000 ; i++) {
        if (signal_pulse_data[i][0] > 0) {
            //fprintf(stderr, "%d\n", signal_pulse_data[i][1]);
        }
    }
    /* 50% decision limit */
    if (max/min > 1) {
        fprintf(stderr, "Pulse coding: Short pulse length %d - Long pulse length %d\n", min, max);
        signal_type = 2;
    } else {
        fprintf(stderr, "Distance coding: Pulse length %d\n", (min+max)/2);
        signal_type = 1;
    }
    p_limit = (max+min)/2;

    /* Initial guesses */
    a[0] = 1000000;
    a[2] = 0;
    for (i=1 ; i<1000 ; i++) {
        if (signal_pulse_data[i][0] > 0) {
//               fprintf(stderr, "[%03d] s: %d\t  e:\t %d\t l:%d\t  d:%d\n",
//               i, signal_pulse_data[i][0], signal_pulse_data[i][1],
//               signal_pulse_data[i][2], signal_pulse_data[i][0]-signal_pulse_data[i-1][1]);
            signal_distance_data[i-1] = signal_pulse_data[i][0]-signal_pulse_data[i-1][1];
            if (signal_distance_data[i-1] > a[2])
                a[2] = signal_distance_data[i-1];
            if (signal_distance_data[i-1] <= a[0])
                a[0] = signal_distance_data[i-1];
        }
    }
    min = a[0];
    max = a[2];
    a[1] = (a[0]+a[2])/2;
//    for (i=0 ; i<1 ; i++) {
//        b[i] = (a[i]+a[i+1])/2;
//    }
    b[0] = (a[0]+a[1])/2;
    b[1] = (a[1]+a[2])/2;
//     fprintf(stderr, "a[0]: %d\t a[1]: %d\t a[2]: %d\t\n",a[0],a[1],a[2]);
//     fprintf(stderr, "b[0]: %d\t b[1]: %d\n",b[0],b[1]);

    k=1;
    delta = 10000000;
    while((k < 10) && (delta > 0)) {
        for (i=0 ; i<3 ; i++) {
            a_new[i] = 0;
            a_cnt[i] = 0;
        }

        for (i=0 ; i < 1000 ; i++) {
            if (signal_distance_data[i] > 0) {
                if (signal_distance_data[i] < b[0]) {
                    a_new[0] += signal_distance_data[i];
                    a_cnt[0]++;
                } else if (signal_distance_data[i] < b[1] && signal_distance_data[i] >= b[0]){
                    a_new[1] += signal_distance_data[i];
                    a_cnt[1]++;
                } else if (signal_distance_data[i] >= b[1]) {
                    a_new[2] += signal_distance_data[i];
                    a_cnt[2]++;
                }
            }
        }

//         fprintf(stderr, "Iteration %d.", k);
        delta = 0;
        for (i=0 ; i<3 ; i++) {
            if (a_cnt[i])
                a_new[i] /= a_cnt[i];
            delta += (a[i]-a_new[i])*(a[i]-a_new[i]);
//             fprintf(stderr, "\ta[%d]: %d (%d)", i, a_new[i], a[i]);
            a[i] = a_new[i];
        }
//         fprintf(stderr, " delta %d\n", delta);

        if (a[0] < min) {
            a[0] = min;
//             fprintf(stderr, "Fixing a[0] = %d\n", min);
        }
        if (a[2] > max) {
            a[0] = max;
//             fprintf(stderr, "Fixing a[2] = %d\n", max);
        }
//         if (a[1] == 0) {
//             a[1] = (a[2]+a[0])/2;
//             fprintf(stderr, "Fixing a[1] = %d\n", a[1]);
//         }

//         fprintf(stderr, "Iteration %d.", k);
        for (i=0 ; i<2 ; i++) {
//             fprintf(stderr, "\tb[%d]: (%d) ", i, b[i]);
            b[i] = (a[i]+a[i+1])/2;
//             fprintf(stderr, "%d  ", b[i]);
        }
//         fprintf(stderr, "\n");
        k++;
    }

    if (override_short) {
        p_limit = override_short;
        a[0] = override_short;
    }

    if (override_long) {
        a[1] = override_long;
    }

    fprintf(stderr, "\nShort distance: %d, long distance: %d, packet distance: %d\n",a[0],a[1],a[2]);
    fprintf(stderr, "\np_limit: %d\n",p_limit);

    demod_reset_bits_packet(&p);
    if (signal_type == 1) {
        for(i=0 ; i<1000 ; i++){
            if (signal_distance_data[i] > 0) {
                if (signal_distance_data[i] < (a[0]+a[1])/2) {
//                     fprintf(stderr, "0 [%d] %d < %d\n",i, signal_distance_data[i], (a[0]+a[1])/2);
                    demod_add_bit(&p, 0);
                } else if ((signal_distance_data[i] > (a[0]+a[1])/2) && (signal_distance_data[i] < (a[1]+a[2])/2)) {
//                     fprintf(stderr, "0 [%d] %d > %d\n",i, signal_distance_data[i], (a[0]+a[1])/2);
                    demod_add_bit(&p, 1);
                } else if (signal_distance_data[i] > (a[1]+a[2])/2) {
//                     fprintf(stderr, "0 [%d] %d > %d\n",i, signal_distance_data[i], (a[1]+a[2])/2);
                    demod_next_bits_packet(&p);
                }

             }

        }
        demod_print_bits_packet(&p);
    }
    if (signal_type == 2) {
        for(i=0 ; i<1000 ; i++){
            if(signal_pulse_data[i][2] > 0) {
                if (signal_pulse_data[i][2] < p_limit) {
//                     fprintf(stderr, "0 [%d] %d < %d\n",i, signal_pulse_data[i][2], p_limit);
                    demod_add_bit(&p, 0);
                } else {
//                     fprintf(stderr, "1 [%d] %d > %d\n",i, signal_pulse_data[i][2], p_limit);
                    demod_add_bit(&p, 1);
                }
                if ((signal_distance_data[i] >= (a[1]+a[2])/2)) {
//                     fprintf(stderr, "\\n [%d] %d > %d\n",i, signal_distance_data[i], (a[1]+a[2])/2);
                    demod_next_bits_packet(&p);
                }


            }
        }
        demod_print_bits_packet(&p);
    }

    for (i=0 ; i<1000 ; i++) {
        signal_pulse_data[i][0] = 0;
        signal_pulse_data[i][1] = 0;
        signal_pulse_data[i][2] = 0;
        signal_distance_data[i] = 0;
    }

};


static void pwm_analyze(struct dm_state *demod, int16_t *buf, uint32_t len)
{
    unsigned int i;

    for (i=0 ; i<len ; i++) {
        if (buf[i] > demod->level_limit) {
            if (!signal_start)
                signal_start = counter;
            if (print) {
                pulses_found++;
                pulse_start = counter;
                signal_pulse_data[signal_pulse_counter][0] = counter;
                signal_pulse_data[signal_pulse_counter][1] = -1;
                signal_pulse_data[signal_pulse_counter][2] = -1;
                if (debug_output) fprintf(stderr, "pulse_distance %d\n",counter-pulse_end);
                if (debug_output) fprintf(stderr, "pulse_start distance %d\n",pulse_start-prev_pulse_start);
                if (debug_output) fprintf(stderr, "pulse_start[%d] found at sample %d, value = %d\n",pulses_found, counter, buf[i]);
                prev_pulse_start = pulse_start;
                print =0;
                print2 = 1;
            }
        }
        counter++;
        if (buf[i] < demod->level_limit) {
            if (print2) {
                pulse_avg += counter-pulse_start;
                if (debug_output) fprintf(stderr, "pulse_end  [%d] found at sample %d, pulse length = %d, pulse avg length = %d\n",
                        pulses_found, counter, counter-pulse_start, pulse_avg/pulses_found);
                pulse_end = counter;
                print2 = 0;
                signal_pulse_data[signal_pulse_counter][1] = counter;
                signal_pulse_data[signal_pulse_counter][2] = counter-pulse_start;
                signal_pulse_counter++;
                if (signal_pulse_counter >= 4000) {
                    signal_pulse_counter = 0;
                    goto err;
                }
            }
            print = 1;
            if (signal_start && (pulse_end + 50000 < counter)) {
                signal_end = counter - 40000;
                fprintf(stderr, "*** signal_start = %d, signal_end = %d\n",signal_start-10000, signal_end);
                fprintf(stderr, "signal_len = %d,  pulses = %d\n", signal_end-(signal_start-10000), pulses_found);
                pulses_found = 0;
                classify_signal();

                signal_pulse_counter = 0;
                if (demod->sg_buf) {
                    int start_pos, signal_bszie, wlen, wrest=0, sg_idx, idx;
                    char sgf_name[256] = {0};
                    FILE *sgfp;

                    sprintf(sgf_name, "gfile%03d.data",demod->signal_grabber);
                    demod->signal_grabber++;
                    signal_bszie = 2*(signal_end-(signal_start-10000));
                    signal_bszie = (131072-(signal_bszie%131072)) + signal_bszie;
                    sg_idx = demod->sg_index-demod->sg_len;
                    if (sg_idx < 0)
                        sg_idx = SIGNAL_GRABBER_BUFFER-demod->sg_len;
                    idx = (i-40000)*2;
                    start_pos = sg_idx+idx-signal_bszie;
                    fprintf(stderr, "signal_bszie = %d  -      sg_index = %d\n", signal_bszie, demod->sg_index);
                    fprintf(stderr, "start_pos    = %d  -   buffer_size = %d\n", start_pos, SIGNAL_GRABBER_BUFFER);
                    if (signal_bszie > SIGNAL_GRABBER_BUFFER)
                        fprintf(stderr, "Signal bigger then buffer, signal = %d > buffer %d !!\n", signal_bszie, SIGNAL_GRABBER_BUFFER);

                    if (start_pos < 0) {
                        start_pos = SIGNAL_GRABBER_BUFFER+start_pos;
                        fprintf(stderr, "restart_pos = %d\n", start_pos);
                    }

                    fprintf(stderr, "*** Saving signal to file %s\n",sgf_name);
                    sgfp = fopen(sgf_name, "wb");
                    if (!sgfp) {
                        fprintf(stderr, "Failed to open %s\n", sgf_name);
                    }
                    wlen = signal_bszie;
                    if (start_pos + signal_bszie > SIGNAL_GRABBER_BUFFER) {
                        wlen = SIGNAL_GRABBER_BUFFER - start_pos;
                        wrest = signal_bszie - wlen;
                    }
                    fprintf(stderr, "*** Writing data from %d, len %d\n",start_pos, wlen);
                    fwrite(&demod->sg_buf[start_pos], 1, wlen, sgfp);

                    if (wrest) {
                        fprintf(stderr, "*** Writing data from %d, len %d\n",0, wrest);
                        fwrite(&demod->sg_buf[0], 1, wrest,  sgfp);
                    }

                    fclose(sgfp);
                }
                signal_start = 0;
            }
        }


    }
    return;

err:
    fprintf(stderr, "To many pulses detected, probably bad input data or input parameters\n");
    return;
}

/* The distance between pulses decodes into bits */

static void pwm_d_decode(struct dm_state *demod, struct protocol_state* p, int16_t *buf, uint32_t len) {
    unsigned int i;

    for (i=0 ; i<len ; i++) {
        if (buf[i] > demod->level_limit) {
            p->pulse_count = 1;
            p->start_c = 1;
        }
        if (p->pulse_count && (buf[i] < demod->level_limit)) {
            p->pulse_length = 0;
            p->pulse_distance = 1;
            p->sample_counter = 0;
            p->pulse_count = 0;
        }
        if (p->start_c) p->sample_counter++;
        if (p->pulse_distance && (buf[i] > demod->level_limit)) {
            if (p->sample_counter < p->short_limit) {
                demod_add_bit(p, 0);
            } else if (p->sample_counter < p->long_limit) {
                demod_add_bit(p, 1);
            } else {
                demod_next_bits_packet(p);
                p->pulse_count    = 0;
                p->sample_counter = 0;
            }
            p->pulse_distance = 0;
        }
        if (p->sample_counter > p->reset_limit) {
            p->start_c    = 0;
            p->sample_counter = 0;
            p->pulse_distance = 0;
            if (p->callback)
                events+=p->callback(p->bits_buffer);
            else
                demod_print_bits_packet(p);

            demod_reset_bits_packet(p);
        }
    }
}

/* The length of pulses decodes into bits */

static void pwm_p_decode(struct dm_state *demod, struct protocol_state* p, int16_t *buf, uint32_t len) {
    unsigned int i;

    for (i=0 ; i<len ; i++) {
        if (buf[i] > demod->level_limit && !p->start_bit) {
            /* start bit detected */
            p->start_bit      = 1;
            p->start_c        = 1;
            p->sample_counter = 0;
//            fprintf(stderr, "start bit pulse start detected\n");
        }

        if (!p->real_bits && p->start_bit && (buf[i] < demod->level_limit)) {
            /* end of startbit */
            p->real_bits = 1;
//            fprintf(stderr, "start bit pulse end detected\n");
        }
        if (p->start_c) p->sample_counter++;


        if (!p->pulse_start && p->real_bits && (buf[i] > demod->level_limit)) {
            /* save the pulse start, it will never be zero */
            p->pulse_start = p->sample_counter;
//           fprintf(stderr, "real bit pulse start detected\n");

        }

        if (p->real_bits && p->pulse_start && (buf[i] < demod->level_limit)) {
            /* end of pulse */

            p->pulse_length = p->sample_counter-p->pulse_start;
//           fprintf(stderr, "real bit pulse end detected %d\n", p->pulse_length);
//           fprintf(stderr, "space duration %d\n", p->sample_counter);

            if (p->pulse_length <= p->short_limit) {
                demod_add_bit(p, 1);
            } else if (p->pulse_length > p->short_limit) {
                demod_add_bit(p, 0);
            }
            p->sample_counter = 0;
            p->pulse_start    = 0;
        }

        if (p->real_bits && p->sample_counter > p->long_limit) {
            demod_next_bits_packet(p);

            p->start_bit = 0;
            p->real_bits = 0;
        }

        if (p->sample_counter > p->reset_limit) {
            p->start_c = 0;
            p->sample_counter = 0;
            //demod_print_bits_packet(p);
            if (p->callback)
                events+=p->callback(p->bits_buffer);
            else
                demod_print_bits_packet(p);
            demod_reset_bits_packet(p);

            p->start_bit = 0;
            p->real_bits = 0;
        }
    }
}



/** Something that might look like a IIR lowpass filter
 *
 *  [b,a] = butter(1, 0.01) ->  quantizes nicely thus suitable for fixed point
 *  Q1.15*Q15.0 = Q16.15
 *  Q16.15>>1 = Q15.14
 *  Q15.14 + Q15.14 + Q15.14 could possibly overflow to 17.14
 *  but the b coeffs are small so it wont happen
 *  Q15.14>>14 = Q15.0 \o/
 */

static uint16_t lp_xmem[FILTER_ORDER] = {0};

#define F_SCALE 15
#define S_CONST (1<<F_SCALE)
#define FIX(x) ((int)(x*S_CONST))

int a[FILTER_ORDER+1] = {FIX(1.00000),FIX(0.96907)};
int b[FILTER_ORDER+1] = {FIX(0.015466),FIX(0.015466)};

static void low_pass_filter(uint16_t *x_buf, int16_t *y_buf, uint32_t len)
{
    unsigned int i;

    /* Calculate first sample */
    y_buf[0] = ((a[1]*y_buf[-1]>>1) + (b[0]*x_buf[0]>>1) + (b[1]*lp_xmem[0]>>1)) >> (F_SCALE-1);
    for (i=1 ; i<len ; i++) {
        y_buf[i] = ((a[1]*y_buf[i-1]>>1) + (b[0]*x_buf[i]>>1) + (b[1]*x_buf[i-1]>>1)) >> (F_SCALE-1);
    }

    /* Save last sample */
    memcpy(lp_xmem, &x_buf[len-1-FILTER_ORDER], FILTER_ORDER*sizeof(int16_t));
    memcpy(&y_buf[-FILTER_ORDER], &y_buf[len-1-FILTER_ORDER], FILTER_ORDER*sizeof(int16_t));
    //fprintf(stderr, "%d\n", y_buf[0]);
}


static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
    struct dm_state *demod = ctx;
    uint16_t* sbuf = (uint16_t*) buf;
    int i;
    if (demod->file || !demod->save_data) {
        if (do_exit || do_exit_async)
            return;

        if ((bytes_to_read > 0) && (bytes_to_read < len)) {
            len = bytes_to_read;
            do_exit = 1;
            rtlsdr_cancel_async(dev);
        }

        if (demod->signal_grabber) {
            //fprintf(stderr, "[%d] sg_index - len %d\n", demod->sg_index, len );
            memcpy(&demod->sg_buf[demod->sg_index], buf, len);
            demod->sg_len =len;
            demod->sg_index +=len;
            if (demod->sg_index+len > SIGNAL_GRABBER_BUFFER)
                demod->sg_index = 0;
        }


        if (demod->debug_mode == 0) {
            envelope_detect(buf, len, demod->decimation_level);
            low_pass_filter(sbuf, demod->f_buf, len>>(demod->decimation_level+1));
        } else if (demod->debug_mode == 1){
            memcpy(demod->f_buf, buf, len);
        }
        if (demod->analyze) {
            pwm_analyze(demod, demod->f_buf, len/2);
        } else {
            for (i=0 ; i<demod->r_dev_num ; i++) {
                switch (demod->r_devs[i]->modulation) {
                    case OOK_PWM_D:
                        pwm_d_decode(demod, demod->r_devs[i], demod->f_buf, len/2);
                        break;
                    case OOK_PWM_P:
                        pwm_p_decode(demod, demod->r_devs[i], demod->f_buf, len/2);
                        break;
                    default:
                        fprintf(stderr, "Unknown modulation %d in protocol!\n", demod->r_devs[i]->modulation);
                }
            }
        }

        if (demod->save_data) {
            if (fwrite(demod->f_buf, 1, len>>demod->decimation_level, demod->file) != len>>demod->decimation_level) {
                fprintf(stderr, "Short write, samples lost, exiting!\n");
                rtlsdr_cancel_async(dev);
            }
        }

        if (bytes_to_read > 0)
            bytes_to_read -= len;

        if(frequencies>1) {
            time_t rawtime;
            time(&rawtime);
            if(difftime(rawtime, rawtime_old)>DEFAULT_HOP_TIME || events>=DEFAULT_HOP_EVENTS) {
                rawtime_old=rawtime;
                events=0;
                do_exit_async=1;
                rtlsdr_cancel_async(dev);
            }
        }
    }
}

int main(int argc, char **argv)
{
#ifndef _WIN32
    struct sigaction sigact;
#endif
    char *filename = NULL;
    char *test_mode_file = NULL;
    FILE *test_mode;
    int n_read;
    int r, opt;
    int i, gain = 0;
    int sync_mode = 0;
    int ppm_error = 0;
    struct dm_state* demod;
    uint8_t *buffer;
    uint32_t dev_index = 0;
    int frequency_current=0;
    uint32_t out_block_size = DEFAULT_BUF_LENGTH;
    int device_count;
    char vendor[256], product[256], serial[256];

    demod = malloc(sizeof(struct dm_state));
    memset(demod,0,sizeof(struct dm_state));

    /* initialize tables */
    calc_squares();

    demod->f_buf = &demod->filter_buffer[FILTER_ORDER];
    demod->decimation_level = DEFAULT_DECIMATION_LEVEL;
    demod->level_limit      = DEFAULT_LEVEL_LIMIT;


    while ((opt = getopt(argc, argv, "x:z:p:Dtam:r:c:l:d:f:g:s:b:n:S::")) != -1) {
        switch (opt) {
        case 'd':
            dev_index = atoi(optarg);
            break;
        case 'f':
            if(frequencies<MAX_PROTOCOLS) frequency[frequencies++] = (uint32_t)atof(optarg);
            else fprintf(stderr, "Max number of frequencies reached %d\n",MAX_PROTOCOLS);
            break;
        case 'g':
            gain = (int)(atof(optarg) * 10); /* tenths of a dB */
            break;
        case 'p':
            ppm_error = atoi(optarg);
            break;
        case 's':
            samp_rate = (uint32_t)atof(optarg);
            break;
        case 'b':
            out_block_size = (uint32_t)atof(optarg);
            break;
        case 'l':
            demod->level_limit = (uint32_t)atof(optarg);
            break;
        case 'n':
            bytes_to_read = (uint32_t)atof(optarg) * 2;
            break;
        case 'c':
            demod->decimation_level = (uint32_t)atof(optarg);
            break;
        case 'a':
            demod->analyze = 1;
            break;
        case 'r':
            test_mode_file = optarg;
            break;
        case 't':
            demod->signal_grabber = 1;
            break;
        case 'm':
            demod->debug_mode = atoi(optarg);
            break;
        case 'S':
            sync_mode = 1;
            break;
        case 'D':
            debug_output = 1;
            break;
        case 'z':
            override_short = atoi(optarg);
            break;
        case 'x':
            override_long = atoi(optarg);
            break;
        default:
            usage();
            break;
        }
    }

    /* init protocols somewhat ok */
    register_protocol(demod, &rubicson);
    register_protocol(demod, &prologue);
    register_protocol(demod, &silvercrest);
//    register_protocol(demod, &generic_hx2262);
//    register_protocol(demod, &technoline_ws9118);
    register_protocol(demod, &elv_em1000);
    register_protocol(demod, &elv_ws2000);
    register_protocol(demod, &waveman);

    if (argc <= optind-1) {
        usage();
    } else {
        filename = argv[optind];
    }

    if(out_block_size < MINIMAL_BUF_LENGTH ||
       out_block_size > MAXIMAL_BUF_LENGTH ){
        fprintf(stderr,
            "Output block size wrong value, falling back to default\n");
        fprintf(stderr,
            "Minimal length: %u\n", MINIMAL_BUF_LENGTH);
        fprintf(stderr,
            "Maximal length: %u\n", MAXIMAL_BUF_LENGTH);
        out_block_size = DEFAULT_BUF_LENGTH;
    }

    buffer = malloc(out_block_size * sizeof(uint8_t));

    device_count = rtlsdr_get_device_count();
    if (!device_count) {
        fprintf(stderr, "No supported devices found.\n");
        if (!test_mode_file)
            exit(1);
    }

    fprintf(stderr, "Found %d device(s):\n", device_count);
    for (i = 0; i < device_count; i++) {
        rtlsdr_get_device_usb_strings(i, vendor, product, serial);
        fprintf(stderr, "  %d:  %s, %s, SN: %s\n", i, vendor, product, serial);
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "Using device %d: %s\n",
        dev_index, rtlsdr_get_device_name(dev_index));

    r = rtlsdr_open(&dev, dev_index);
    if (r < 0) {
        fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev_index);
        if (!test_mode_file)
            exit(1);
    }
#ifndef _WIN32
    sigact.sa_handler = sighandler;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigaction(SIGINT, &sigact, NULL);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGQUIT, &sigact, NULL);
    sigaction(SIGPIPE, &sigact, NULL);
#else
    SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif
    /* Set the sample rate */
    r = rtlsdr_set_sample_rate(dev, samp_rate);
    if (r < 0)
        fprintf(stderr, "WARNING: Failed to set sample rate.\n");
    else
        fprintf(stderr, "Sample rate set to %d.\n", rtlsdr_get_sample_rate(dev)); // Unfortunately, doesn't return real rate

    fprintf(stderr, "Sample rate decimation set to %d. %d->%d\n",demod->decimation_level, samp_rate, samp_rate>>demod->decimation_level);
    fprintf(stderr, "Bit detection level set to %d.\n", demod->level_limit);

    if (0 == gain) {
         /* Enable automatic gain */
        r = rtlsdr_set_tuner_gain_mode(dev, 0);
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to enable automatic gain.\n");
        else
            fprintf(stderr, "Tuner gain set to Auto.\n");
    } else {
        /* Enable manual gain */
        r = rtlsdr_set_tuner_gain_mode(dev, 1);
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to enable manual gain.\n");

        /* Set the tuner gain */
        r = rtlsdr_set_tuner_gain(dev, gain);
        if (r < 0)
            fprintf(stderr, "WARNING: Failed to set tuner gain.\n");
        else
            fprintf(stderr, "Tuner gain set to %f dB.\n", gain/10.0);
    }

    r = rtlsdr_set_freq_correction(dev, ppm_error);

    demod->save_data = 1;
    if (!filename) {
        demod->save_data = 0;
    } else if(strcmp(filename, "-") == 0) { /* Write samples to stdout */
        demod->file = stdout;
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
    } else {
        demod->file = fopen(filename, "wb");
        if (!demod->file) {
            fprintf(stderr, "Failed to open %s\n", filename);
            goto out;
        }
    }

    if (demod->signal_grabber)
        demod->sg_buf = malloc(SIGNAL_GRABBER_BUFFER);

    if (test_mode_file) {
        int i = 0;
        unsigned char test_mode_buf[DEFAULT_BUF_LENGTH];
        fprintf(stderr, "Test mode active. Reading samples from file: %s\n",test_mode_file);
        test_mode = fopen(test_mode_file, "r");
        if (!test_mode) {
            fprintf(stderr, "Opening file: %s failed!\n",test_mode_file);
            goto out;
        }
        while(fread(test_mode_buf, 131072, 1, test_mode) != 0) {
            rtlsdr_callback(test_mode_buf, 131072, demod);
            i++;
        }
        //Always classify a signal at the end of the file
        classify_signal();
        fprintf(stderr, "Test mode file issued %d packets\n", i);
        fprintf(stderr, "Filter coeffs used:\n");
        fprintf(stderr, "a: %d %d\n", a[0], a[1]);
        fprintf(stderr, "b: %d %d\n", b[0], b[1]);
        exit(0);
    }

    /* Reset endpoint before we start reading from it (mandatory) */
    r = rtlsdr_reset_buffer(dev);
    if (r < 0)
        fprintf(stderr, "WARNING: Failed to reset buffers.\n");

    if (sync_mode) {
        fprintf(stderr, "Reading samples in sync mode...\n");
        while (!do_exit) {
            r = rtlsdr_read_sync(dev, buffer, out_block_size, &n_read);
            if (r < 0) {
                fprintf(stderr, "WARNING: sync read failed.\n");
                break;
            }

            if ((bytes_to_read > 0) && (bytes_to_read < (uint32_t)n_read)) {
                n_read = bytes_to_read;
                do_exit = 1;
            }

            if (fwrite(buffer, 1, n_read, demod->file) != (size_t)n_read) {
                fprintf(stderr, "Short write, samples lost, exiting!\n");
                break;
            }

            if ((uint32_t)n_read < out_block_size) {
                fprintf(stderr, "Short read, samples lost, exiting!\n");
                break;
            }

            if (bytes_to_read > 0)
                bytes_to_read -= n_read;
        }
    } else {
        if(frequencies==0) {
          frequency[0] = DEFAULT_FREQUENCY;
          frequencies=1;
        } else {
          time(&rawtime_old);
        }
        fprintf(stderr, "Reading samples in async mode...\n");
        while(!do_exit) {
            /* Set the frequency */
            r = rtlsdr_set_center_freq(dev, frequency[frequency_current]);
            if (r < 0)
                fprintf(stderr, "WARNING: Failed to set center freq.\n");
            else
                fprintf(stderr, "Tuned to %u Hz.\n", rtlsdr_get_center_freq(dev));
            r = rtlsdr_read_async(dev, rtlsdr_callback, (void *)demod,
                          DEFAULT_ASYNC_BUF_NUMBER, out_block_size);
            do_exit_async=0;
            frequency_current++;
            if(frequency_current>frequencies-1) frequency_current=0;
        }
    }

    if (do_exit)
        fprintf(stderr, "\nUser cancel, exiting...\n");
    else
        fprintf(stderr, "\nLibrary error %d, exiting...\n", r);

    if (demod->file && (demod->file != stdout))
        fclose(demod->file);

    for (i=0 ; i<demod->r_dev_num ; i++)
        free(demod->r_devs[i]);

    if (demod->signal_grabber)
        free(demod->sg_buf);

    if(demod)
        free(demod);

    rtlsdr_close(dev);
    free (buffer);
out:
    return r >= 0 ? r : -r;
}
