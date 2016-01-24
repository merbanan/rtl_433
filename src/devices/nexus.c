#include "rtl_433.h"
#include "data.h"
#include "util.h"

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
    data_t *data;

    char time_str[LOCAL_TIME_BUFLEN];

    if (debug_output > 1) {
       fprintf(stderr,"Possible Nexus: ");
       bitbuffer_print(bitbuffer);
    }

    int16_t temp2;
    float temp;
    uint8_t humidity;
    uint8_t id;

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

        /* Get time now */
        local_time_str(0, time_str);

        /* Nibble 0,1 contains id */
        id = bb[5][0];

        /* Nible 3,4,5 contains 12 bits of temperature
         * The temerature is signed and scaled by 10 */
        temp2 = (int16_t)((uint16_t)(bb[5][1] << 12) | (bb[5][2] << 4));
        temp2 = temp2 >> 4;
        temp = temp2/10.;
        humidity = (uint8_t)(((bb[5][3]&0x0F)<<4)|(bb[5][4]>>4));

        if (debug_output > 1) {
            fprintf(stderr, "ID          = 0x%2X\n",  id);
            fprintf(stdout, "Humidity    = %u\n", humidity);
            fprintf(stdout, "Temperature = %.02f\n", temp);
        }

        // Thermo
        if (bb[5][3] == 0xF0) {
        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "Nexus Temperature",
                         "id",            "House Code",  DATA_INT, id,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp,
                         NULL);
        data_acquired_handler(data);
        }
        // Thermo/Hygro
        else {
        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "Nexus Temperature/Humidity",
                         "id",            "House Code",  DATA_INT, id,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp,
                         "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                         NULL);
        data_acquired_handler(data);
        }
        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "temperature_C",
    "humidity",
    NULL
};


// timings based on samp_rate=1024000
r_device nexus = {
    .name           = "Nexus Temperature & Humidity Sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 1744,
    .long_limit     = 3500,
    .reset_limit    = 5000,
    .json_callback  = &nexus_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};

