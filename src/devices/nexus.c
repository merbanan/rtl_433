#include "rtl_433.h"
extern int rubicson_crc_check(bitrow_t *bb);

/* Currently this can decode the temperature and id from Nexus sensors
 *
 * the sensor sends 36 bits 12 times pwm modulated
 * the data is grouped into 9 nibles
 *
 * The id changes when the battery is changed in the sensor.
 * unk0 is always 1 0 0 0, most likely 2 channel bits as the sensor can recevice 3 channels
 * unk1-3 changes and the meaning is unknown
 * temp is 12 bit signed scaled by 10
 *
 * The sensor can be bought at Clas Ohlsen
 */

static int nexus_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    int temperature_before_dec;
    int temperature_after_dec;
    int16_t temp;
    int16_t humidity;

    /** The nexus protocol will trigger on rubicson data, so calculate the rubicson crc and make sure
      * it doesn't match. By guesstimate it should generate a correct crc 1/255% of the times.
      * So less then 0.5% which should be acceptable.
      */
    if (!rubicson_crc_check(bb) && ((bb[1][0] == bb[2][0] && bb[2][0] == bb[3][0] && bb[3][0] == bb[4][0] &&
        bb[4][0] == bb[5][0] && bb[5][0] == bb[6][0] && bb[6][0] == bb[7][0] && bb[7][0] == bb[8][0] &&
        bb[8][0] == bb[9][0] && (bb[5][0] != 0 && bb[5][1] != 0 && bb[5][2] != 0 && bb[12][1] != 0x80)) &&
        (bb[1][4] == bb[2][4] && bb[2][4] == bb[3][4] && bb[3][4] == bb[4][4] &&
        bb[4][4] == bb[5][4] && bb[5][4] == bb[6][4] && bb[6][4] == bb[7][4] && bb[7][4] == bb[8][4] &&
        bb[8][4] == bb[9][4] && (bb[5][2] != 0 && bb[5][3] != 0 ) && ((bb[5][4]&0x0F) == 0)))) {

        /* Nible 3,4,5 contains 12 bits of temperature
         * The temerature is signed and scaled by 10 */
        temp = (int16_t)((uint16_t)(bb[5][1] << 12) | (bb[5][2] << 4));
        temp = temp >> 4;

        temperature_before_dec = abs(temp / 10);
        temperature_after_dec = abs(temp % 10);
        humidity = (int16_t)(((bb[5][3]&0x0F)<<4)|(bb[5][4]>>4));

        fprintf(stdout, "Sensor temperature event:\n");
        fprintf(stdout, "protocol: Nexus\n");
        fprintf(stdout, "Temp    : %s%d.%d\n",temp<0?"-":"",temperature_before_dec,temperature_after_dec);
        fprintf(stdout, "Humidity: %d\n", humidity);
		fprintf(stdout, "%02x %02x %02x %02x %02x = %s%d.%d\n",bb[1][0],bb[0][1],bb[0][2],bb[0][3],bb[0][4],temp<0?"-":"",temperature_before_dec, temperature_after_dec);

        return 1;
    }
    return 0;
}

// timings based on samp_rate=1024000
r_device nexus = {
    .name           = "Nexus Temperature & Humidity Sensor",
    .modulation     = OOK_PWM_D,
    .short_limit    = 1744/4,
    .long_limit     = 3500/4,
    .reset_limit    = 5000/4,
    .json_callback  = &nexus_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};

