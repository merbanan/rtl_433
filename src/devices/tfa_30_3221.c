/*
    Temperature/Humidity outdoor sensor TFA 30.3221.02 
    -------------------------------------------------------------------------------------------------
    source : https://github.com/RFD-FHEM/RFFHEM/blob/master/FHEM/14_SD_WS.pm
    0    4    | 8    12   | 16   20   | 24   28   | 32   36  
    0000 1001 | 0001 0110 | 0001 0000 | 0000 0111 | 0100 1001
    iiii iiii | bscc tttt | tttt tttt | hhhh hhhh | ???? ????
    i:  8 bit random id (changes on power-loss)
    b:  1 bit battery indicator (0=>OK, 1=>LOW)
    s:  1 bit sendmode (0=>auto, 1=>manual)
    c:  2 bit channel valid channels are 0-2 (1-3)
    t: 12 bit unsigned temperature, offset 500, scaled by 10
    h:  8 bit relative humidity percentage
    ?:  8 bit unknown
    The sensor sends 3 repetitions at intervals of about 60 seconds
*/

#include "decoder.h"

static int tfa_303221_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row, sendmode, channel, battery_low, temp_raw, humidity;
    double temp_c;
    data_t *data;
    uint8_t *bytes;
    unsigned int device;

    /* Device send 4 row, checking for two repeated */ 
    row = bitbuffer_find_repeated_row(bitbuffer, (bitbuffer->num_rows > 4) ? 4 : 2, 40);

    /* Checking for right number of bits per row*/
    if (bitbuffer->bits_per_row[0] != 41)
        return DECODE_ABORT_LENGTH;

    bitbuffer_invert(bitbuffer);
    bytes = bitbuffer->bb[row];

    device      = bytes[0];

    /* Sanity Check */
    if (device == 0)
        return DECODE_FAIL_SANITY;

    temp_raw    = ((bytes[1] & 0x0F) << 8) | bytes[2];
    temp_c      = (double)(temp_raw - 500)/10;
    humidity    = bytes[3];
    battery_low = bytes[1] >> 7;
    channel     = ((bytes[1] >> 4) & 3) + 1;
    sendmode    = (bytes[1] >> 6) & 1;

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "TFA-30322102",
            "id",               "Sensor ID",    DATA_INT,    device,
            "channel",          "Channel",      DATA_INT,    channel,
            "battery_ok",       "Battery",      DATA_INT,    !battery_low,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.2f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "sendmode",         "Test ?",       DATA_STRING,    sendmode ? "Yes" : "No",
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "battery_ok",
        "temperature_C",
        "humidity",
        "sendmode",
        NULL,
};

r_device tfa_30_3221 = {
        .name        = "TFA Dostmann 30.3221 T/H Outdoor Sensor",
        .modulation  = OOK_PULSE_PWM,
        .short_width = 235,
        .long_width  = 480,
        .reset_limit = 850,
        .sync_width  = 836,
        .decode_fn   = &tfa_303221_callback,
        .disabled    = 0,
        .fields      = output_fields,
};