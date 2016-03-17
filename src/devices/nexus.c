/* Nexus sensor protocol with ID, temperature and optional humidity
 * also FreeTec NC-7345 sensors for FreeTec Weatherstation NC-7344.
 *
 * the sensor sends 36 bits 12 times,
 * the packets are ppm modulated (distance coding) with a pulse of ~500 us
 * followed by a short gap of ~1000 us for a 0 bit or a long ~2000 us gap for a
 * 1 bit, the sync gap is ~4000 us.
 *
 * the data is grouped in 9 nibbles
 * [id0] [id1] [flags] [temp0] [temp1] [temp2] [const] [humi0] [humi1]
 *
 * The 8-bit id changes when the battery is changed in the sensor.
 * flags are 4 bits B 0 C C, where B is the battery status: 1=OK, 0=LOW
 * and CC is the channel: 0=CH1, 1=CH2, 2=CH3
 * temp is 12 bit signed scaled by 10
 * const is always 1111 (0x0F)
 * humiditiy is 8 bits
 *
 * The sensor can be bought at Clas Ohlsen
 */

#include "rtl_433.h"
#include "util.h"
#include "data.h"

extern int rubicson_crc_check(bitrow_t *bb);

static int nexus_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;

    char time_str[LOCAL_TIME_BUFLEN];

    if (debug_output > 1) {
       fprintf(stderr,"Possible Nexus: ");
       bitbuffer_print(bitbuffer);
    }

    uint8_t id;
    uint8_t battery;
    uint8_t channel;
    int16_t temp;
    uint8_t humidity;
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 36);

    /** The nexus protocol will trigger on rubicson data, so calculate the rubicson crc and make sure
      * it doesn't match. By guesstimate it should generate a correct crc 1/255% of the times.
      * So less then 0.5% which should be acceptable.
      */
    if (r >= 0 &&
        bitbuffer->bits_per_row[r] <= 37 && // we expect 36 bits but there might be a trailing 0 bit
        bb[r][0] != 0 &&
        bb[r][2] != 0 &&
        bb[r][3] != 0 &&
        !rubicson_crc_check(bb)) {

        /* Get time now */
        local_time_str(0, time_str);

        /* Nibble 0,1 contains id */
        id = bb[r][0];

        /* Nibble 2 is battery and channel */
        battery = bb[r][1]&0x80;
        channel = ((bb[r][1]&0x30) >> 4) + 1;

        /* Nible 3,4,5 contains 12 bits of temperature
         * The temerature is signed and scaled by 10 */
        temp = (int16_t)((uint16_t)(bb[r][1] << 12) | (bb[r][2] << 4));
        temp = temp >> 4;

        /* Nibble 6,7 is humidity */
        humidity = (uint8_t)(((bb[r][3]&0x0F)<<4)|(bb[r][4]>>4));

        // Thermo
        if (bb[r][3] == 0xF0) {
        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "Nexus Temperature",
                         "id",            "House Code",  DATA_INT, id,
                         "battery",       "Battery",     DATA_STRING, battery ? "OK" : "LOW",
                         "channel",       "Channel",     DATA_INT, channel,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp/10.0,
                         NULL);
        data_acquired_handler(data);
        }
        // Thermo/Hygro
        else {
        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "Nexus Temperature/Humidity",
                         "id",            "House Code",  DATA_INT, id,
                         "battery",       "Battery",     DATA_STRING, battery ? "OK" : "LOW",
                         "channel",       "Channel",     DATA_INT, channel,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp/10.0,
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
    "battery",
    "channel",
    "temperature_C",
    "humidity",
    NULL
};

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
