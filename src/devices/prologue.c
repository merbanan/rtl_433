/* Prologue sensor protocol
 * also FreeTec NC-7104 sensor for FreeTec Weatherstation NC-7102.
 *
 * the sensor sends 36 bits 7 times, before the first packet there is a sync pulse
 * the packets are ppm modulated (distance coding) with a pulse of ~500 us
 * followed by a short gap of ~2000 us for a 0 bit or a long ~4000 us gap for a
 * 1 bit, the sync gap is ~9000 us.
 *
 * the data is grouped in 9 nibbles
 * [model] [id0] [id1] [flags] [temp0] [temp1] [temp2] [humi0] [humi1]
 *
 * model is 1001 (9) or 0110 (5)
 * id is a random id that is generated when the sensor starts, could include battery status
 * the same batteries often generate the same id
 * flags(3) is 0 the battery status, 1 ok, 0 low, first reading always say low
 * flags(2) is 1 when the sensor sends a reading when pressing the button on the sensor
 * flags(1,0)+1 forms the channel number that can be set by the sensor (1-3)
 * temp is 12 bit signed scaled by 10
 * humi0 is always 1100 (0x0C) if no humidity sensor is available
 * humi1 is always 1100 (0x0C) if no humidity sensor is available
 *
 * The sensor can be bought at Clas Ohlson
 */

#include "rtl_433.h"
#include "util.h"
#include "data.h"

static int prologue_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;

    char time_str[LOCAL_TIME_BUFLEN];

    uint8_t model;
    uint8_t id;
    uint8_t battery;
    uint8_t button;
    uint8_t channel;
    int16_t temp;
    uint8_t humidity;
    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 36);

    if (r >= 0 &&
        bitbuffer->bits_per_row[r] <= 37 && // we expect 36 bits but there might be a trailing 0 bit
        ((bb[r][0]&0xF0) == 0x90 ||
         (bb[r][0]&0xF0) == 0x50)) {

        /* Get time now */
        local_time_str(0, time_str);

        /* Prologue sensor */
        model = bb[r][0] >> 4;
        id = ((bb[r][0]&0x0F)<<4) | ((bb[r][1]&0xF0)>>4);
        battery = bb[r][1]&0x08;
        button = (bb[r][1]&0x04) >> 2;
        channel = (bb[r][1]&0x03) + 1;
        temp = (int16_t)((uint16_t)(bb[r][2] << 8) | (bb[r][3]&0xF0));
        temp = temp >> 4;
        humidity = ((bb[r][3]&0x0F) << 4) | (bb[r][4] >> 4);

        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "Prologue sensor",
                         "id",            "",            DATA_INT, model, // this should be named "type"
                         "rid",           "",            DATA_INT, id, // this should be named "id"
                         "channel",       "Channel",     DATA_INT, channel,
                         "battery",       "Battery",     DATA_STRING, battery ? "OK" : "LOW",
                         "button",        "Button",      DATA_INT, button,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp/10.0,
                         "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                          NULL);
        data_acquired_handler(data);

        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "rid",
    "channel",
    "battery",
    "button",
    "temperature_C",
    "humidity",
    NULL
};

r_device prologue = {
    .name           = "Prologue Temperature Sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 3500,
    .long_limit     = 7000,
    .reset_limit    = 10000,
    .json_callback  = &prologue_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};
