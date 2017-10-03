#include "rtl_433.h"
#include "util.h"

static int mebus433_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    char    time_str[LOCAL_TIME_BUFLEN];
    int16_t temp;
    int8_t  hum;
    uint8_t address;
    uint8_t channel;
    uint8_t battery;
    uint8_t unknown1;
    uint8_t unknown2;
    data_t *data;

    if (bb[0][0] == 0 && bb[1][4] !=0 && (bb[1][0] & 0x60) && bb[1][3]==bb[5][3] && bb[1][4] == bb[12][4]){
        local_time_str(0, time_str);

        address = bb[1][0] & 0x1f;

        channel = ((bb[1][1] & 0x30) >> 4) + 1;
        // Always 0?
        unknown1 = (bb[1][1] & 0x40) >> 6;
        battery = bb[1][1] & 0x80;

        // Upper 4 bits are stored in nibble 1, lower 8 bits are stored in nibble 2
        // upper 4 bits of nibble 1 are reserved for other usages.
        temp = (int16_t)((uint16_t)(bb[1][1] << 12) | bb[1][2] << 4);
        temp = temp >> 4;
        // lower 4 bits of nibble 3 and upper 4 bits of nibble 4 contains
        // humidity as decimal value
        hum  = (bb[1][3] << 4 | bb[1][4] >> 4);

        // Always 0b1111?
        unknown2 = (bb[1][3] & 0xf0) >> 4;

        data = data_make("time",          "",            DATA_STRING, time_str,
                         "model",         "",            DATA_STRING, "Mebus/433",
                         "id",            "Address",     DATA_INT, address,
                         "battery",       "Battery",     DATA_STRING, battery ? "OK" : "LOW",
                         "channel",       "Channel",     DATA_INT, channel,
                         "unknown1",      "Unknown 1",   DATA_INT, unknown1,
                         "unknown2",      "Unknown 2",   DATA_INT, unknown2,
                         "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp / 10.0,
                         "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, hum,
                         NULL);
        data_acquired_handler(data);


        return 1;
    }
    return 0;
}

r_device mebus433 = {
    .name           = "Mebus 433",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 1200,
    .long_limit     = 2400,
    .reset_limit    = 6000,
    .json_callback  = &mebus433_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};
