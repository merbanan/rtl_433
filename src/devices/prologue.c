/* Prologue sensor protocol
 *
 * the sensor sends 36 bits 7 times, before the first packet there is a pulse sent
 * the packets are pwm modulated
 *
 * the data is grouped in 9 nibles
 * [id0] [rid0] [rid1] [data0] [temp0] [temp1] [temp2] [humi0] [humi1]
 *
 * id0 is 1001,9 or 0110,5
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
#include "util.h"
#include "data.h"

static int prologue_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;

    char time_str[LOCAL_TIME_BUFLEN];

    uint8_t rid;
    uint8_t id;
    uint8_t channel;
    uint8_t button;
    uint8_t battery;
    int16_t temp2;
    float temp;
    uint8_t humidity;

    /* FIXME validate the received message better */
    if (((bb[1][0]&0xF0) == 0x90 && (bb[2][0]&0xF0) == 0x90 && (bb[3][0]&0xF0) == 0x90 && (bb[4][0]&0xF0) == 0x90 &&
        (bb[5][0]&0xF0) == 0x90 && (bb[6][0]&0xF0) == 0x90) ||
        ((bb[1][0]&0xF0) == 0x50 && (bb[2][0]&0xF0) == 0x50 && (bb[3][0]&0xF0) == 0x50 && (bb[4][0]&0xF0) == 0x50 &&
        (bb[1][3] == bb[2][3]) && (bb[1][4] == bb[2][4]))) {

        /* Get time now */
        local_time_str(0, time_str);

        /* Prologue sensor */
        id = (bb[1][0]&0xF0)>>4;
        rid = ((bb[1][0]&0x0F)<<4) | ((bb[1][1]&0xF0)>>4);
        battery = bb[1][1]&0x08;
        channel = (bb[1][1]&0x03) + 1;
        button = (bb[1][1]&0x04) >> 2;
        temp2 = (int16_t)((uint16_t)(bb[1][2] << 8) | (bb[1][3]&0xF0));
        temp2 = temp2 >> 4;
        temp = temp2/10.;
        humidity = ((bb[1][3]&0x0F)<<4) | (bb[1][4]>>4);

        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "Prologue sensor",
                         "id",            "",            DATA_INT, id,
                         "rid",           "",            DATA_INT, rid,
                         "channel",       "Channel",     DATA_INT, channel,
                         "battery",       "Battery",     DATA_STRING, battery ? "OK" : "LOW",
                         "button",        "Button",      DATA_INT, button,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp,
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
    .short_limit    = 3500/4,
    .long_limit     = 7000/4,
    .reset_limit    = 2500,
    .json_callback  = &prologue_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};
