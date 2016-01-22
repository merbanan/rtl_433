#include "rtl_433.h"

/* Currently this can decode the temperature and id from Rubicson sensors
 *
 * the sensor sends 36 bits 12 times pwm modulated
 * the data is grouped into 9 nibles
 * [id0] [id1], [bat|unk1|chan1|chan2] [temp0], [temp1] [temp2], [F] [crc1], [crc2]
 *
 * The id changes when the battery is changed in the sensor.
 * bat bit is 1 if battery is ok, 0 if battery is low
 * chan1 and chan2 forms a 2bit value for the used channel
 * unk1 is always 0 probably unused
 * temp is 12 bit signed scaled by 10
 * F is always 0xf
 * crc1 and crc2 forms a 8-bit crc
 *
 * The sensor can be bought at Kjell&Co
 */


/* Working routine for checking the crc, lots of magic but it works */

//static uint8_t rp[] = {0xb8, 0x80, 0xea, 0xfe, 0x80};
static uint8_t rp[] = {0xea, 0x8f, 0x6a, 0xfa, 0x50};

int rubicson_crc_check(bitrow_t *bb) {
    uint8_t crc, w;
    uint8_t diff[9];
    int i, ret;

    // diff against ref packet

    diff[0] = rp[0]^bb[1][0];
    diff[1] = rp[1]^bb[1][1];
    diff[2] = rp[2]^bb[1][2];
    diff[3] = rp[3]^bb[1][3];
    diff[4] = rp[4]^bb[1][4];

//    fprintf(stdout, "%02x %02x %02x %02x %02x\n",rp[0],rp[1],rp[2],rp[3],rp[4]);
//    fprintf(stdout, "%02x %02x %02x %02x %02x\n",bb[1][0],bb[1][1],bb[1][2],bb[1][3],bb[1][4]);
//    fprintf(stdout, "%02x %02x %02x %02x %02x\n",diff[0],diff[1],diff[2],diff[3],diff[4]);

    for (crc = 0, w = 0xf1, i = 0; i<7 ; i++){
        uint8_t c = diff[i/2];
        unsigned digit = (i&1) ? c&0xF : (c&0xF0)>>4;
        unsigned j;
        for (j=4; j-->0; ) {
            if ((digit >> j) & 1)
                crc ^= w;
            w = (w >> 1) ^ ((w & 1) ? 0x98: 0);
        }
    }
    if (crc == (((diff[3]<<4)&0xF0) | (diff[4]>>4)))
//      printf ("\ncrc ok: %x\n", crc);
        ret = 1;
    else
//      printf ("\ncrc fail: %x\n", crc);
        ret = 0;

	return ret;
};

static int rubicson_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    int temperature_before_dec;
    int temperature_after_dec, i;
    int16_t temp;
    int8_t rh, csum, csum_calc, sum=0;

    if (rubicson_crc_check(bb)) {

        /* Nible 3,4,5 contains 12 bits of temperature
         * The temerature is signed and scaled by 10 */
        temp = (int16_t)((uint16_t)(bb[0][1] << 12) | (bb[0][2] << 4));
        temp = temp >> 4;

        temperature_before_dec = abs(temp / 10);
        temperature_after_dec = abs(temp % 10);

        fprintf(stdout, "Sensor temperature event:\n");
        fprintf(stdout, "protocol       = Rubicson\n");
        fprintf(stdout, "battery        = %s\n",(bb[0][1]&0x80) ? "ok" : "low");
        fprintf(stdout, "channel        = %d\n",((bb[0][1]&0x30)>>4)+1);
        fprintf(stdout, "rid            = %02x\n",bb[0][0]);
        fprintf(stdout, "temp           = %s%d.%d\n",temp<0?"-":"",temperature_before_dec, temperature_after_dec);
        fprintf(stdout, "crc            = ok\n");
//        fprintf(stdout, "%02x %02x %02x %02x %02x = %s%d.%d\n",bb[1][0],bb[0][1],bb[0][2],bb[0][3],bb[0][4],temp<0?"-":"",temperature_before_dec, temperature_after_dec);

        return 1;
    }
    return 0;
}

// timings based on samp_rate=1024000
r_device rubicson = {
    .name           = "Rubicson Temperature Sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 488+970,      // Gaps:  Short 976µs, Long 1940µs, Sync 4000µs
    .long_limit     = 970+2000,     // Pulse: 500µs (Initial pulse in each package is 388µs)
    .reset_limit    = 4800,             // Two initial pulses and a gap of 9120µs is filtered out
    .json_callback  = &rubicson_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};

