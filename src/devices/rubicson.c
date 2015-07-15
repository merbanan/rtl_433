#include "rtl_433.h"

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

static int rubicson_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
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

        fprintf(stdout, "Sensor temperature event:\n");
        fprintf(stdout, "protocol       = Rubicson/Auriol, %d bits\n",bitbuffer->bits_per_row[1]);
        fprintf(stdout, "rid            = %x\n",bb[0][0]);
        fprintf(stdout, "temp           = %s%d.%d\n",temp<0?"-":"",temperature_before_dec, temperature_after_dec);
        fprintf(stdout, "%02x %02x %02x %02x %02x\n",bb[1][0],bb[0][1],bb[0][2],bb[0][3],bb[0][4]);

        return 1;
    }
    return 0;
}

// timings based on samp_rate=1024000
r_device rubicson = {
    .name           = "Rubicson Temperature Sensor",
    .modulation     = OOK_PWM_D,
    .short_limit    = 1744/4,
    .long_limit     = 3500/4,
    .reset_limit    = 5000/4,
    .json_callback  = &rubicson_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};

