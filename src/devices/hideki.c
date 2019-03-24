/* Hideki Temperature, Humidity, Wind, Rain sensor
 *
 * The received bits are inverted.
 * Every 8 bits are stuffed with a (even) parity bit.
 * The payload (excluding the header) has an byte parity (XOR) check
 * The payload (excluding the header) has CRC-8, poly 0x07 init 0x00 check
 * The payload bytes are reflected (LSB first / LSB last) after the CRC check
 *
 *    11111001 0  11110101 0  01110011 1 01111010 1  11001100 0  01000011 1  01000110 1  00111111 0  00001001 0  00010111 0
 *    SYNC+HEAD P   RC cha P     LEN   P     Nr.? P   .1° 1°  P   10°  BV P   1%  10% P     ?     P     XOR   P     CRC   P
 *
 * TS04:
 *    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888 99999999
 *    SYNC+HEAD cha   RC     LEN        Nr.?    1° .1°  VB   10°   10%  1%     ?         XOR      CRC
 *
 * Wind:
 *    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888 99999999 AAAAAAAA BBBBBBBB CCCCCCCC DDDDDDDD
 *    SYNC+HEAD cha   RC     LEN        Nr.?    1° .1°  VB   10°    1° .1°  VB   10°   1W .1W  .1G 10W   10G 1G    w°  AA    XOR      CRC
 *
 * Rain:
 *    00000000  11111111  22222222  33333333  44444444  55555555  66666666  77777777  88888888
 *    SYNC+HEAD cha   RC   B LEN        Nr.?   RAIN_L    RAIN_H     0x66       XOR       CRC
 *
 */

#include "decoder.h"

#define HIDEKI_MAX_BYTES_PER_ROW 14

enum sensortypes { HIDEKI_UNKNOWN, HIDEKI_TEMP, HIDEKI_TS04, HIDEKI_WIND, HIDEKI_RAIN };

static int hideki_ts04_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t *b = bitbuffer->bb[0]; // TODO: handle the 3 row, need change in PULSE_CLOCK decoding
    uint8_t packet[HIDEKI_MAX_BYTES_PER_ROW];
    int sensortype, chk;
    int channel, rc, battery_ok;
    int temp, humidity, rain_units;
    int wind_speed, gust_speed, wind_direction, wind_approach;

    // Expect 8, 9, 10, or 14 unstuffed bytes
    int unstuffed_len = bitbuffer->bits_per_row[0] / 9;
    if (unstuffed_len == 14)
        sensortype = HIDEKI_WIND;
    else if (unstuffed_len == 10)
        sensortype = HIDEKI_TS04;
    else if (unstuffed_len == 9)
        sensortype = HIDEKI_RAIN;
    else if (unstuffed_len == 8)
        sensortype = HIDEKI_TEMP;
    else
        return 0;

    // Invert all bits
    bitbuffer_invert(bitbuffer);

    // Strip (unstuff) and check parity bit
    // TODO: refactor to util function
    for (int i = 0; i < unstuffed_len; ++i) {
        unsigned int offset = i/8;
        packet[i] = (b[i+offset] << (i%8)) | (b[i+offset+1] >> (8 - i%8));
        // check parity
        uint8_t parity = (b[i+offset+1] >> (7 - i%8)) & 1;
        if (parity != parity8(packet[i])) {
            if (decoder->verbose)
                fprintf(stderr, "%s: Parity error at %d\n", __func__, i);
            return 0;
        }
    }

    // XOR check all bytes
    chk = xor_bytes(&packet[1], unstuffed_len - 2);
    if (chk) {
        if (decoder->verbose)
            fprintf(stderr, "%s: XOR error\n", __func__);
        return 0;
    }

    // CRC-8 poly=0x07 init=0x00
    if (crc8(&packet[1], unstuffed_len - 1, 0x07, 0x00)) {
        if (decoder->verbose)
            fprintf(stderr, "%s: CRC error\n", __func__);
        return 0;
    }

    // Reflect LSB first to LSB last
    reflect_bytes(packet, unstuffed_len);

    // Parse data
    if (packet[0] != 0x9f) // NOTE: other valid ids might exist
        return 0;

    int pkt_len  = (packet[2] >> 1) & 0x1f;
    int pkt_seq  = packet[3] >> 6;
    int pkt_type = packet[3] & 0x1f;
    // 0x0C Anemometer
    // 0x0D UV sensor
    // 0x0E Rain level meter
    // 0x1E Thermo/hygro-sensor

    if (pkt_len +3 != unstuffed_len) {
        if (decoder->verbose)
            fprintf(stderr, "%s: LEN error\n", __func__);
        return 0;
    }

    channel = (packet[1] >> 5) & 0x0F;
    if (channel >= 5) channel -= 1;
    rc = packet[1] & 0x0F;
    temp = (packet[5] & 0x0F) * 100 + ((packet[4] & 0xF0) >> 4) * 10 + (packet[4] & 0x0F);
    if (((packet[5]>>7) & 1) == 0) {
        temp = -temp;
    }
    battery_ok = (packet[5]>>6) & 1;

    if (sensortype == HIDEKI_TS04) {
        humidity = ((packet[6] & 0xF0) >> 4) * 10 + (packet[6] & 0x0F);
        data = data_make(
                "model",            "",                 DATA_STRING, _X("Hideki-TS04","HIDEKI TS04 sensor"),
                _X("id","rc"),               "Rolling Code",     DATA_INT, rc,
                "channel",          "Channel",          DATA_INT, channel,
                "battery",          "Battery",          DATA_STRING, battery_ok ? "OK": "LOW",
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp/10.f,
                "humidity",         "Humidity",         DATA_FORMAT, "%u %%", DATA_INT, humidity,
                "mic",              "MIC",              DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    if (sensortype == HIDEKI_WIND) {
        int const wd[] = { 0, 15, 13, 14, 9, 10, 12, 11, 1, 2, 4, 3, 8, 7, 5, 6 };
        wind_direction = wd[((packet[11] & 0xF0) >> 4)] * 225;
        wind_speed = (packet[9] & 0x0F) * 100 + (packet[8] >> 4) * 10 + (packet[8] & 0x0F);
        gust_speed = (packet[10] & 0xF0) * 100 + (packet[10] >> 4) * 10 + (packet[9] >> 4);
        int const ad[] = { 0, 1, -1, 2 }; // i.e. None, CW, CCW, invalid
        wind_approach = ad[(packet[11] >> 2) & 0x03];

        data = data_make(
                "model",            "",                 DATA_STRING, _X("Hideki-Wind","HIDEKI Wind sensor"),
                _X("id","rc"),               "Rolling Code",     DATA_INT, rc,
                "channel",          "Channel",          DATA_INT, channel,
                "battery",          "Battery",          DATA_STRING, battery_ok ? "OK": "LOW",
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp * 0.1f,
                "wind_speed_mph",   "Wind Speed",       DATA_FORMAT, "%.02f mph", DATA_DOUBLE, wind_speed * 0.1f,
                "gust_speed_mph",   "Gust Speed",       DATA_FORMAT, "%.02f mph", DATA_DOUBLE, gust_speed * 0.1f,
                "wind_approach",    "Wind Approach",    DATA_INT, wind_approach,
                "wind_direction",   "Wind Direction",   DATA_FORMAT, "%.01f °", DATA_DOUBLE, wind_direction * 0.1f,
                "mic",              "MIC",              DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    if (sensortype == HIDEKI_TEMP) {
        data = data_make(
                "model",            "",                 DATA_STRING, _X("Hideki-Temperature","HIDEKI Temperature sensor"),
                _X("id","rc"),               "Rolling Code",     DATA_INT, rc,
                "channel",          "Channel",          DATA_INT, channel,
                "battery",          "Battery",          DATA_STRING, battery_ok ? "OK": "LOW",
                "temperature_C",    "Temperature",      DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp * 0.1f,
                "mic",              "MIC",              DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    if (sensortype == HIDEKI_RAIN) {
        rain_units = (packet[5] << 8) | packet[4];
        battery_ok = (packet[2] >> 6) & 1;

        data = data_make(
                "model",            "",                 DATA_STRING, _X("Hideki-Rain","HIDEKI Rain sensor"),
                _X("id","rc"),               "Rolling Code",     DATA_INT, rc,
                "channel",          "Channel",          DATA_INT, channel,
                "battery",          "Battery",          DATA_STRING, battery_ok ? "OK": "LOW",
                "rain_mm",          "Rain",             DATA_FORMAT, "%.01f mm", DATA_DOUBLE, rain_units * 0.7f,
                "mic",              "MIC",              DATA_STRING, "CRC",
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "model",
    "rc", // TODO: delete this
    "id",
    "channel",
    "battery",
    "temperature_C",
    "humidity",
    "wind_speed_mph",
    "gust_speed_mph",
    "wind_approach",
    "wind_direction",
    "rain_mm",
    "mic",
    NULL
};

r_device hideki_ts04 = {
    .name           = "HIDEKI TS04 Temperature, Humidity, Wind and Rain Sensor",
    .modulation     = OOK_PULSE_DMC,
    .short_width    = 520,  // half-bit width 520 us
    .long_width     = 1040, // bit width 1040 us
    .reset_limit    = 4000,
    .tolerance      = 240,
    .decode_fn      = &hideki_ts04_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
