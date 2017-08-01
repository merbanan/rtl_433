/*
 * Outdoor sensor transmits data temperature, humidity.
 * Transmissions also includes an id. The sensor transmits
 * every 60 seconds 6 packets.
 *
 * 0000 1111 | 0011 0000 | 0101 1100 | 1110 0111 | 0110 0001
 * xxxx xxxx | cccc cccc | tttt tttt | tttt hhhh | hhhh ????
 *
 * x - ID // changes on battery switch
 * c - Unknown Checksum (changes on every transmit if the other values are different)
 * h - Humidity // BCD-encoded, each nibble is one digit
 * t - Temperature   // in °F as binary number with one decimal place + 90 °F offset
 *
 *
 * Usage:
 *
 *    # rtl_433 -f 434052000 -R 91 -F json:log.json
 *
 */

#include "rtl_433.h"
#include "util.h"
#include "data.h"

static int infactory_callback(bitbuffer_t *bitbuffer) {

    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bb[0];
    data_t *data;

    if (bitbuffer->bits_per_row[0] != 40) {
      return 0;
    }

    uint8_t id = b[0];

    uint hum1 = b[3] & 0xF;
    uint hum2 = b[4] >> 4;
    uint humidity = hum1 * 10 + hum2;

    uint8_t temp1 = b[2];
    uint temp2 = b[3] >> 4;
    uint16_t temp = (temp1 << 4) | temp2;

    double temp_f = (double)temp / 10 - 90;

    char time_str[LOCAL_TIME_BUFLEN];
    local_time_str(0, time_str);

    data = data_make( "time",		"",				DATA_STRING,	time_str,
        "model",         "",	   DATA_STRING, "inFactory sensor",
        "id",      "ID",   DATA_FORMAT, "%u", DATA_INT, id,
        "temperature_F", "Temperature",DATA_FORMAT, "%.02f °F", DATA_DOUBLE, temp_f,
        "humidity",      "Humidity",   DATA_FORMAT, "%u %%", DATA_INT, humidity,
        NULL);
    data_acquired_handler(data);


    return 1;
}


static char *output_fields[] = {
    "time",
    "model"
    "id",
    "temperature_F",
    "humidity",
    NULL
};

r_device infactory = {
    .name          = "inFactory",
    .modulation    = OOK_PULSE_PPM_RAW,
    .short_limit   = 3000, // Threshold between short and long gap [us]
    .long_limit    = 5000, //  Maximum gap size before new row of bits [us]
    .reset_limit   = 6000, // Maximum gap size before End Of Message [us].
    .json_callback = &infactory_callback,
    .disabled      = 1,
    .fields        = output_fields
};
