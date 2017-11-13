#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"
#include "data.h"

#define HIDEKI_BYTES_PER_ROW 14

//    11111001 0  11110101 0  01110011 1 01111010 1  11001100 0  01000011 1  01000110 1  00111111  0 00001001 0  00010111 0
//    SYNC+HEAD P   RC cha P           P     Nr.? P   .1° 1°  P   10°  BV P   1%  10% P  ????SYNC    -------Check?------- P

//TS04:
//    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888 99999999
//    SYNC+HEAD cha   RC                Nr.?    1° .1°  VB   10°   10%  1%  SYNC????  -----Check?------

//Wind:
//    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888 99999999 AAAAAAAA BBBBBBBB CCCCCCCC DDDDDDDD
//    SYNC+HEAD cha   RC                Nr.?    1° .1°  VB   10°    1° .1°  VB   10°  .1mh 1mh  ?? 10mh   ????    w°  ??    ????     ????

enum sensortypes { HIDEKI_UNKNOWN, HIDEKI_TS04, HIDEKI_WIND, HIDEKI_RAIN };

static int hideki_ts04_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bb[0];//TODO: handle the 3 row, need change in PULSE_CLOCK decoding

    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    local_time_str(0, time_str);

    uint8_t packet[HIDEKI_BYTES_PER_ROW];
    int sensortype = HIDEKI_WIND; // default for 14 valid bytes
    uint8_t channel, humidity, rc, battery_ok;
    int temp, wind_strength, wind_direction, rain_units;

    // Transform the incoming data:
    //  * change endianness
    //  * toggle each bits
    //  * Remove (and check) parity bit
    // TODO this may be factorise as a bitbuffer method (as for bitbuffer_manchester_decode)
    for(int i=0; i<HIDEKI_BYTES_PER_ROW; i++){
        unsigned int offset = i/8;
        packet[i] = b[i+offset] << (i%8);
        packet[i] |= b[i+offset+1] >> (8 - i%8);
        // reverse as it is litle endian...
        packet[i] = reverse8(packet[i]);
        // toggle each bit
        packet[i] ^= 0xFF;
        // check parity
        uint8_t parity = ((b[i+offset+1] >> (7 - i%8)) ^ 0xFF) & 0x01;
        if(parity != byteParity(packet[i]))
        {
            if (i == 10) {
                sensortype = HIDEKI_TS04;
                break;
            }
            if (i == 9) {
                sensortype = HIDEKI_RAIN;
                break;
            }
            return 0;
        }
    }

    // Read data
    if(packet[0] == 0x9f){ //Note: it may exist other valid id
        channel = (packet[1] >> 5) & 0x0F;
        if(channel >= 5) channel -= 1;
        rc = packet[1] & 0x0F;
        temp = (packet[5] & 0x0F) * 100 + ((packet[4] & 0xF0) >> 4) * 10 + (packet[4] & 0x0F);
        if(((packet[5]>>7) & 0x01) == 0){
            temp = -temp;
        }
        battery_ok = (packet[5]>>6) & 0x01;
        if (sensortype == HIDEKI_TS04) {
            humidity = ((packet[6] & 0xF0) >> 4) * 10 + (packet[6] & 0x0F);
            data = data_make("time",          "",              DATA_STRING, time_str,
                             "model",         "",              DATA_STRING, "HIDEKI TS04 sensor",
                             "rc",            "Rolling Code",  DATA_INT, rc,
                             "channel",       "Channel",       DATA_INT, channel,
                             "battery",       "Battery",       DATA_STRING, battery_ok ? "OK": "LOW",
                             "temperature_C", "Temperature",   DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp/10.f,
                             "humidity",      "Humidity",      DATA_FORMAT, "%u %%", DATA_INT, humidity,
                             NULL);
            data_acquired_handler(data);
            return 1;
        }
        if (sensortype == HIDEKI_WIND) {
            const uint8_t wd[] = { 0, 15, 13, 14, 9, 10, 12, 11, 1, 2, 4, 3, 8, 7, 5, 6 };
            wind_direction = wd[((packet[11] & 0xF0) >> 4)] * 225;
            wind_strength = (packet[9] & 0x0F) * 100 + ((packet[8] & 0xF0) >> 4) * 10 + (packet[8] & 0x0F);
            data = data_make("time",          "",              DATA_STRING, time_str,
                             "model",         "",              DATA_STRING, "HIDEKI Wind sensor",
                             "rc",            "Rolling Code",  DATA_INT, rc,
                             "channel",       "Channel",       DATA_INT, channel,
                             "battery",       "Battery",       DATA_STRING, battery_ok ? "OK": "LOW",
                             "temperature_C", "Temperature",   DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp/10.f,
                             "windstrength",  "Wind Strength", DATA_FORMAT, "%.02f km/h", DATA_DOUBLE, wind_strength*0.160934f,
                             "winddirection", "Direction",     DATA_FORMAT, "%.01f °", DATA_DOUBLE, wind_direction/10.f,
                             NULL);
            data_acquired_handler(data);
            return 1;
        }
        if (sensortype == HIDEKI_RAIN) {
            rain_units = (packet[5] << 8) + packet[4];
            battery_ok = (packet[2]>>6) & 0x01;
            data = data_make("time",          "",              DATA_STRING, time_str,
                             "model",         "",              DATA_STRING, "HIDEKI Rain sensor",
                             "rc",            "Rolling Code",  DATA_INT, rc,
                             "channel",       "Channel",       DATA_INT, channel,
                             "battery",       "Battery",       DATA_STRING, battery_ok ? "OK": "LOW",
                             "rain",          "Rain",          DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rain_units*0.7f,
                             NULL);
            data_acquired_handler(data);
            return 1;
        }
        return 0;
    }
    return 0;
}

PWM_Precise_Parameters hideki_ts04_clock_bits_parameters = {
    .pulse_tolerance    = 240, // us
    .pulse_sync_width    = 0,    // No sync bit used
};

static char *output_fields[] = {
    "time",
    "model",
    "rc",
    "channel",
    "battery",
    "temperature_C",
    "humidity",
    "windstrength",
    "winddirection",
    "rain",
    NULL
};

r_device hideki_ts04 = {
    .name           = "HIDEKI TS04 Temperature, Humidity, Wind and Rain Sensor",
    .modulation     = OOK_PULSE_CLOCK_BITS,
    .short_limit    = 520,
    .long_limit     = 1040, // not used
    .reset_limit    = 4000,
    .json_callback  = &hideki_ts04_callback,
    .disabled       = 0,
    .demod_arg     = (uintptr_t)&hideki_ts04_clock_bits_parameters,
    .fields         = output_fields,
};
