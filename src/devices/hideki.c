#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"
#include "data.h"

#define HIDEKI_BYTES_PER_ROW 10

//    11111001 0  11110101 0  01110011 1 01111010 1  11001100 0  01000011 1  01000110 1  00111111  0 00001001 0  00010111 0
//    SYNC+HEAD P   RC cha P           P     Nr.? P   .1° 1°  P   10°  BV P   1%  10% P  ????SYNC    -------Check?------- P

//    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888 99999999
//    SYNC+HEAD cha   RC                Nr.?    1° .1°  VB   10°   10%  1%  SYNC????  -----Check?------

static int hideki_ts04_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bb[0];//TODO: handle the 3 row, need change in PULSE_CLOCK decoding

    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    local_time_str(0, time_str);

    uint8_t packet[HIDEKI_BYTES_PER_ROW];

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
            return 0;
    }

    // Read data
    if(packet[0] == 0x9f){ //Note: it may exist other valid id
        uint8_t channel = (packet[1] >> 5) & 0x0F;
        if(channel >= 5) channel -= 1;
        uint8_t rc = packet[1] & 0x0F;
        int temp = (packet[5] & 0x0F) * 100 + ((packet[4] & 0xF0) >> 4) * 10 + (packet[4] & 0x0F);
        if(((packet[5]>>7) & 0x01) == 0){
            temp = -temp;
        }
        uint8_t humidity = ((packet[6] & 0xF0) >> 4) * 10 + (packet[6] & 0x0F);
        uint8_t battery_ok = (packet[5]>>6) & 0x01;

        data = data_make("time",          "",       DATA_STRING, time_str,
                "model",         "",              DATA_STRING, "HIDEKI TS04 sensor",
                "rc",            "Rolling Code",  DATA_INT, rc,
                "channel",       "Channel",       DATA_INT, channel,
                "battery",       "Battery",       DATA_STRING, battery_ok ? "OK": "LOW",
                "temperature_C", "Temperature",   DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp/10.f,
                "humidity",      "Humidity",      DATA_FORMAT, "%u %%", DATA_INT, humidity,
                NULL);
        data_acquired_handler(data);
        return 1;
    }
    return 0;
}

PWM_Precise_Parameters hideki_ts04_clock_bits_parameters = {
   .pulse_tolerance    = 60,
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
    NULL
};

r_device hideki_ts04 = {
    .name           = "HIDEKI TS04 Temperature and Humidity Sensor",
    .modulation     = OOK_PULSE_CLOCK_BITS,
    .short_limit    = 520,
    .long_limit     = 1040, // not used
    .reset_limit    = 4000,
    .json_callback  = &hideki_ts04_callback,
    .disabled       = 0,
    .demod_arg     = (uintptr_t)&hideki_ts04_clock_bits_parameters,
    .fields         = output_fields,
};
