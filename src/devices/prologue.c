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
#include "rtl_433.h"

static int prologue_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    int rid;

    int16_t temp2;

    /* FIXME validate the received message better */
    if (((bb[1][0]&0xF0) == 0x90 && (bb[2][0]&0xF0) == 0x90 && (bb[3][0]&0xF0) == 0x90 && (bb[4][0]&0xF0) == 0x90 &&
        (bb[5][0]&0xF0) == 0x90 && (bb[6][0]&0xF0) == 0x90) ||
        ((bb[1][0]&0xF0) == 0x50 && (bb[2][0]&0xF0) == 0x50 && (bb[3][0]&0xF0) == 0x50 && (bb[4][0]&0xF0) == 0x50 &&
        (bb[1][3] == bb[2][3]) && (bb[1][4] == bb[2][4]))) {

        /* Prologue sensor */
        temp2 = (int16_t)((uint16_t)(bb[1][2] << 8) | (bb[1][3]&0xF0));
        temp2 = temp2 >> 4;
        fprintf(stdout, "Sensor temperature event:\n");
        fprintf(stdout, "protocol      = Prologue, %d bits\n",bitbuffer->bits_per_row[1]);
        fprintf(stdout, "button        = %d\n",bb[1][1]&0x04?1:0);
        fprintf(stdout, "battery       = %s\n",bb[1][1]&0x08?"Ok":"Low");
        fprintf(stdout, "temp          = %s%d.%d\n",temp2<0?"-":"",abs((int16_t)temp2/10),abs((int16_t)temp2%10));
        fprintf(stdout, "humidity      = %d\n", ((bb[1][3]&0x0F)<<4)|(bb[1][4]>>4));
        fprintf(stdout, "channel       = %d\n",(bb[1][1]&0x03)+1);
        fprintf(stdout, "id            = %d\n",(bb[1][0]&0xF0)>>4);
        rid = ((bb[1][0]&0x0F)<<4)|(bb[1][1]&0xF0)>>4;
        fprintf(stdout, "rid           = %d\n", rid);
        fprintf(stdout, "hrid          = %02x\n", rid);
        return 1;
    }
    return 0;
}

r_device prologue = {
    .name           = "Prologue Temperature Sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 3500/4,
    .long_limit     = 7000/4,
    .reset_limit    = 2500,
    .json_callback  = &prologue_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};
