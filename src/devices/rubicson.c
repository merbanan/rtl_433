/* Currently this can decode the temperature and id from Rubicson sensors
 *
 * the sensor sends 36 bits 12 times pwm modulated
 * the data is grouped into 9 nibbles
 * [id0] [id1], [bat|unk1|chan1|chan2] [temp0], [temp1] [temp2], [F] [crc1], [crc2]
 *
 * The id changes when the battery is changed in the sensor.
 * bat bit is 1 if battery is ok, 0 if battery is low
 * chan1 and chan2 forms a 2bit value for the used channel
 * unk1 is always 0 probably unused
 * temp is 12 bit signed scaled by 10
 * F is always 0xf
 * crc1 and crc2 forms a 8-bit crc, polynomial 0x31, initial value 0x6c, final value 0x0
 *
 * The sensor can be bought at Kjell&Co
 */

#include "decoder.h"

// NOTE: this is used in nexus.c and solight_te44.c
int rubicson_crc_check(bitrow_t *bb) {
    uint8_t tmp[5];
    tmp[0] = bb[1][0];            // Byte 0 is nibble 0 and 1
    tmp[1] = bb[1][1];            // Byte 1 is nibble 2 and 3
    tmp[2] = bb[1][2];            // Byte 2 is nibble 4 and 5
    tmp[3] = bb[1][3]&0xf0;       // Byte 3 is nibble 6 and 0-padding
    tmp[4] = (bb[1][3]&0x0f)<<4 | // CRC is nibble 7 and 8
             (bb[1][4]&0xf0)>>4;
    return crc8(tmp, 5, 0x31, 0x6c) == 0;
}

static int rubicson_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    unsigned bits = bitbuffer->bits_per_row[0];
    data_t *data;

    uint8_t channel;
    uint8_t sensor_id;
    uint8_t battery;
    int16_t temp;
    float temp_c;

    if (!(bits == 36))
        return 0;

    if (rubicson_crc_check(bb)) {

        /* Nibble 3,4,5 contains 12 bits of temperature
         * The temperature is signed and scaled by 10 */
        temp = (int16_t)((uint16_t)(bb[0][1] << 12) | (bb[0][2] << 4));
        temp = temp >> 4;

        channel = ((bb[0][1]&0x30)>>4)+1;
        battery = (bb[0][1]&0x80);
        sensor_id = bb[0][0];
        temp_c = (float) temp / 10.0;

        data = data_make(
                        "model",         "",            DATA_STRING, _X("Rubicson-Temperature","Rubicson Temperature Sensor"),
                        "id",            "House Code",  DATA_INT,    sensor_id,
                        "channel",       "Channel",     DATA_INT,    channel,
                        "battery",       "Battery",     DATA_STRING, battery ? "OK" : "LOW",
                        "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                        "mic",           "Integrity",   DATA_STRING, "CRC",
                        NULL);
        decoder_output_data(decoder, data);

        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "temperature_C",
    "mic",
    NULL
};


// timings based on samp_rate=1024000
r_device rubicson = {
    .name           = "Rubicson Temperature Sensor",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 1000, // Gaps:  Short 976µs, Long 1940µs, Sync 4000µs
    .long_width     = 2000, // Pulse: 500µs (Initial pulse in each package is 388µs)
    .gap_limit      = 3000,
    .reset_limit    = 4800, // Two initial pulses and a gap of 9120µs is filtered out
    .decode_fn      = &rubicson_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
