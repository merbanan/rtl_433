#include "decoder.h"

static uint16_t AD_POP(uint8_t *bb, uint8_t bits, uint8_t bit)
{
    uint16_t val = 0;
    uint8_t i, byte_no, bit_no;
    for (i=0;i<bits;i++) {
        byte_no=   (bit+i)/8 ;
        bit_no =7-((bit+i)%8);
        if (bb[byte_no]&(1<<bit_no)) val = val | (1<<i);
    }
    return val;
}

// based on fs20.c
static int em1000_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    bitrow_t *bb = bitbuffer->bb;
    uint8_t dec[10];
    uint8_t bytes=0;
    uint8_t bit=18; // preamble
    uint8_t bb_p[14];
    char* types[] = {"EM 1000-S", "EM 1000-?", "EM 1000-GZ"};
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
    char *subtype = dec[0] >= 1 && dec[0] <= 3 ? types[dec[0] - 1] : "?";
    int code      = dec[1];
    int seqno     = dec[2];
    int total     = dec[3] | dec[4] << 8;
    int current   = dec[5] | dec[6] << 8;
    int peak      = dec[7] | dec[8] << 8;

    data = data_make(
            "model",    "", DATA_STRING, "ELV-EM1000",
            "id",       "", DATA_STRING, code,
            "seq",      "", DATA_INT, seqno,
            "total",    "", DATA_STRING, total,
            "current",  "", DATA_STRING, current,
            "peak",     "", DATA_FORMAT, peak,
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *elv_em1000_output_fields[] = {
    "model",
    "id"
    "seq",
    "total",
    "current",
    "peak",
    NULL
};

r_device elv_em1000 = {
    .name           = "ELV EM 1000",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 500,  // guessed, no samples available
    .long_width     = 1000, // guessed, no samples available
    .gap_limit      = 7250,
    .reset_limit    = 30000,
    .decode_fn      = &em1000_callback,
    .disabled       = 1,
    .fields         = elv_em1000_output_fields,
};


// based on http://www.dc3yc.privat.t-online.de/protocol.htm
static int ws2000_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    bitrow_t *bb = bitbuffer->bb;
    uint8_t dec[13]={0};
    uint8_t nibbles=0;
    uint8_t bit=11; // preamble
    char* types[]={"!AS3", "AS2000/ASH2000/S2000/S2001A/S2001IA/ASH2200/S300IA", "!S2000R", "!S2000W", "S2001I/S2001ID", "!S2500H", "!Pyrano", "!KS200/KS300"};
    uint8_t length[16]={8, 8, 5, 8, 12, 9, 8, 8, 8};
    uint8_t check_calculated=0, sum_calculated=0;
    uint8_t i;
    uint8_t stopbit;
    uint8_t sum_received;

    dec[0] = AD_POP (bb[0], 4, bit); bit+=4;
    stopbit= AD_POP (bb[0], 1, bit); bit+=1;
    if (!stopbit) {
        if(decoder->verbose) fprintf(stdout, "!stopbit\n");
        return 0;
    }
    check_calculated ^= dec[0];
    sum_calculated   += dec[0];

    // read nibbles with stopbit ...
    for (i = 1; i <= length[dec[0]]; i++) {
        dec[i] = AD_POP (bb[0], 4, bit); bit+=4;
        stopbit= AD_POP (bb[0], 1, bit); bit+=1;
        if (!stopbit) {
            if(decoder->verbose) fprintf(stdout, "!stopbit %i\n", bit);
            return 0;
        }
        check_calculated ^= dec[i];
        sum_calculated   += dec[i];
        nibbles++;
    }
    if(decoder->verbose) { for (i = 0; i < nibbles; i++) fprintf(stdout, "%02X ", dec[i]); fprintf(stdout, "\n"); }

    if (check_calculated) {
        if(decoder->verbose) fprintf(stdout, "check_calculated (%d) != 0\n", check_calculated);
        return 0;
    }

    // Read sum
    sum_received = AD_POP (bb[0], 4, bit); bit+=4;
    sum_calculated+=5;
    sum_calculated&=0xF;
    if (sum_received != sum_calculated) {
        if(decoder->verbose) fprintf(stdout, "sum_received (%d) != sum_calculated (%d) ", sum_received, sum_calculated);
        return 0;
    }

    char *subtype  = dec[0] <= 7 ? types[dec[0]] : "?";
    int code       = dec[1] & 7;
    float temp     = (dec[1] & 8 ? -1.0 : 1.0) * (dec[4] * 10 + dec[3] + dec[2] * 0.1);
    float humidity = dec[7] * 10 + dec[6] + dec[5] * 0.1;
    int pressure   = 0;
    if (dec[0]==4) {
        pressure = 200 + dec[10] * 100 + dec[9] * 10 + dec[8];
    }

    data = data_make(
            "model",        "", DATA_STRING, "ELV-WS2000",
            "id",           "", DATA_INT, code,
            "subtype",      "", DATA_STRING, subtype,
            "temperature",  "", DATA_FORMAT, "%.1f C", DATA_DOUBLE, (double)temp,
            "humidity",     "", DATA_FORMAT, "%.1f %%", DATA_DOUBLE, (double)humidity,
            "pressure",     "", DATA_INT, pressure,
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *elv_ws2000_output_fields[] = {
    "model",
    "id"
    "subtype",
    "temperature",
    "humidity",
    "pressure",
    NULL
};

r_device elv_ws2000 = {
    .name           = "ELV WS 2000",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 366,  // 0 => 854us, 1 => 366us according to link in top
    .long_width     = 854,  // no repetitions
    .reset_limit    = 1000, // Longest pause is 854us according to link
    .decode_fn      = &ws2000_callback,
    .disabled       = 1,
    .fields         = elv_ws2000_output_fields,
};
