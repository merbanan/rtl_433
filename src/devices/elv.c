#include "rtl_433.h"

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

static int em1000_callback(bitbuffer_t *bitbuffer) {
    // based on fs20.c
    bitrow_t *bb = bitbuffer->bb;
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
//            fprintf(stdout, "!stopbit: %i\n", i);
            return 0;
        }
        checksum_calculated ^= dec[i];
        bytes++;
    }

    // Read checksum
    checksum_received = AD_POP (bb_p, 8, bit); bit+=8;
    if (checksum_received != checksum_calculated) {
//        fprintf(stdout, "checksum_received != checksum_calculated: %d %d\n", checksum_received, checksum_calculated);
        return 0;
    }

//for (i = 0; i < bytes; i++) fprintf(stdout, "%02X ", dec[i]); fprintf(stdout, "\n");

    // based on 15_CUL_EM.pm
    fprintf(stdout, "Energy sensor event:\n");
    fprintf(stdout, "protocol      = ELV EM 1000, %d bits\n",bitbuffer->bits_per_row[1]);
    fprintf(stdout, "type          = EM 1000-%s\n",dec[0]>=1&&dec[0]<=3?types[dec[0]-1]:"?");
    fprintf(stdout, "code          = %d\n",dec[1]);
    fprintf(stdout, "seqno         = %d\n",dec[2]);
    fprintf(stdout, "total cnt     = %d\n",dec[3]|dec[4]<<8);
    fprintf(stdout, "current cnt   = %d\n",dec[5]|dec[6]<<8);
    fprintf(stdout, "peak cnt      = %d\n",dec[7]|dec[8]<<8);

    return 1;
}

static int ws2000_callback(bitbuffer_t *bitbuffer) {
    // based on http://www.dc3yc.privat.t-online.de/protocol.htm
    bitrow_t *bb = bitbuffer->bb;
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
//fprintf(stdout, "!stopbit\n");
        return 0;
    }
    check_calculated ^= dec[0];
    sum_calculated   += dec[0];

    // read nibbles with stopbit ...
    for (i = 1; i <= (dec[0]==4?12:8); i++) {
        dec[i] = AD_POP (bb[0], 4, bit); bit+=4;
        stopbit= AD_POP (bb[0], 1, bit); bit+=1;
        if (!stopbit) {
//fprintf(stdout, "!stopbit %i\n", i);
            return 0;
        }
        check_calculated ^= dec[i];
        sum_calculated   += dec[i];
        nibbles++;
    }

    if (check_calculated) {
//fprintf(stdout, "check_calculated (%d) != 0\n", check_calculated);
        return 0;
    }

    // Read sum
    sum_received = AD_POP (bb[0], 4, bit); bit+=4;
    sum_calculated+=5;
    sum_calculated&=0xF;
    if (sum_received != sum_calculated) {
//fprintf(stdout, "sum_received (%d) != sum_calculated (%d) ", sum_received, sum_calculated);
        return 0;
    }

//for (i = 0; i < nibbles; i++) fprintf(stdout, "%02X ", dec[i]); fprintf(stdout, "\n");

    fprintf(stdout, "Weather station sensor event:\n");
    fprintf(stdout, "protocol      = ELV WS 2000, %d bits\n",bitbuffer->bits_per_row[1]);
    fprintf(stdout, "type (!=ToDo) = %s\n", dec[0]<=7?types[dec[0]]:"?");
    fprintf(stdout, "code          = %d\n", dec[1]&7);
    fprintf(stdout, "temp          = %s%d.%d\n", dec[1]&8?"-":"", dec[4]*10+dec[3], dec[2]);
    fprintf(stdout, "humidity      = %d.%d\n", dec[7]*10+dec[6], dec[5]);
    if(dec[0]==4) {
        fprintf(stdout, "pressure      = %d\n", 200+dec[10]*100+dec[9]*10+dec[8]);
    }

    return 1;
}

r_device elv_em1000 = {
    .name           = "ELV EM 1000",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 750,
    .long_limit     = 7250,
    .reset_limit    = 30000,
    .json_callback  = &em1000_callback,
    .disabled       = 1,
    .demod_arg      = 0,
};

r_device elv_ws2000 = {
    .name           = "ELV WS 2000",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 602+(1155-602)/2,
    .long_limit     = (1755635-1655517)/2, // no repetitions
    .reset_limit    = (1755635-1655517)*2,
    .json_callback  = &ws2000_callback,
    .disabled       = 1,
    .demod_arg      = 0,
};
