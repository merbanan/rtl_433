/* Documentation also at http://www.tfd.hu/tfdhu/files/wsprotocol/auriol_protocol_v20.pdf
 * Message Format: (9 nibbles, 36 bits):
 * Please note that bytes need to be reversed before processing!
 *
 * PPM with pulse width 500 us, long gap 4000 us, short gap 2000 us, sync gap 9000 us
 *
 * Format for Temperature Humidity
 *   AAAAAAAA BBBB CCCC CCCC CCCC DDDDDDDD EEEE
 *   RC       Type Temperature___ Humidity Checksum
 *   A = Rolling Code / Device ID
 *       Device ID: AAAABBAA BB is used for channel, base channel is 01
 *       When channel selector is used, channel can be 10 (2) and 11 (3)
 *   B = Message type (xyyz = temp/humidity if yy <> '11') else wind/rain sensor
 *       x indicates battery status (0 normal, 1 voltage is below ~2.6 V)
 *       z 0 indicates regular transmission, 1 indicates requested by pushbutton
 *   C = Temperature (two's complement)
 *   D = Humidity BCD format
 *   E = Checksum
 *
 * Format for Rain
 *   AAAAAAAA BBBB CCCC DDDD DDDD DDDD DDDD EEEE
 *   RC       Type      Rain                Checksum
 *   A = Rolling Code /Device ID
 *   B = Message type (xyyx = NON temp/humidity data if yy = '11')
 *   C = fixed to 1100
 *   D = Rain (bitvalue * 0.25 mm)
 *   E = Checksum
 *
 * Format for Windspeed
 *   AAAAAAAA BBBB CCCC CCCC CCCC DDDDDDDD EEEE
 *   RC       Type                Windspd  Checksum
 *   A = Rolling Code
 *   B = Message type (xyyx = NON temp/humidity data if yy = '11')
 *   C = Fixed to 1000 0000 0000
 *   D = Windspeed  (bitvalue * 0.2 m/s, correction for webapp = 3600/1000 * 0.2 * 100 = 72)
 *   E = Checksum
 *
 * Format for Winddirection & Windgust
 *   AAAAAAAA BBBB CCCD DDDD DDDD EEEEEEEE FFFF
 *   RC       Type      Winddir   Windgust Checksum
 *   A = Rolling Code
 *   B = Message type (xyyx = NON temp/humidity data if yy = '11')
 *   C = Fixed to 111
 *   D = Wind direction
 *   E = Windgust (bitvalue * 0.2 m/s, correction for webapp = 3600/1000 * 0.2 * 100 = 72)
 *   F = Checksum
 *
 */

#include "decoder.h"

/* return 1 if the checksum passes and 0 if it fails */
int alecto_checksum(r_device *decoder, bitrow_t *bb) {
    int i, csum = 0, csum2 = 0;
    for (i = 0; i < 4; i++) {
        uint8_t tmp = reverse8(bb[1][i]);
        csum += (tmp & 0xf) + ((tmp & 0xf0) >> 4);
        tmp = reverse8(bb[5][i]);
        csum2 += (tmp & 0xf) + ((tmp & 0xf0) >> 4);
    }

    csum = ((bb[1][1] & 0x7f) == 0x6c) ? (csum + 0x7) : (0xf - csum);
    csum2 = ((bb[5][1] & 0x7f) == 0x6c) ? (csum2 + 0x7) : (0xf - csum2);

    csum = reverse8((csum & 0xf) << 4);
    csum2 = reverse8((csum2 & 0xf) << 4);
    /* Quit if checksum does not work out */
    if (csum != (bb[1][4] >> 4) || csum2 != (bb[5][4] >> 4)) {
        //fprintf(stdout, "\nAlectoV1 CRC error");
        if(decoder->verbose) {
            fprintf(stderr, "AlectoV1 Checksum/Parity error\n");
        }
        return 0;
    } //Invalid checksum
    if (decoder->verbose){
        fprintf(stdout, "Checksum      = %01x (calculated %01x)\n", bb[1][4] >> 4, csum);
    }

    return 1;
}


static uint8_t bcd_decode8(uint8_t x) {
    return ((x & 0xF0) >> 4) * 10 + (x & 0x0F);
}

static int alectov1_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    int16_t temp;
    uint8_t humidity;
    int ret;

    data_t *data;
    unsigned bits = bitbuffer->bits_per_row[1];

    if (bits != 36)
        return 0;


    if (bb[1][0] == bb[5][0] && bb[2][0] == bb[6][0] && (bb[1][4] & 0xf) == 0 && (bb[5][4] & 0xf) == 0
        && (bb[5][0] != 0 && bb[5][1] != 0)) {

        ret = alecto_checksum(decoder, bb);
        if (!ret)
            return 0;


        uint8_t wind = 0;
        uint8_t channel = (bb[1][0] & 0xc) >> 2;
        uint8_t sensor_id = reverse8(bb[1][0]);
        uint8_t battery_low = bb[1][1]&0x80;

        if ((bb[1][1] & 0xe0) == 0x60) {
            wind = ((bb[1][1] & 0xf) == 0xc) ? 0 : 1;

            //left out data (not needed):
            //bb[1][1]&0x10 ? "timed event":"Button generated ");
            //fprintf(stdout, "Protocol      = AlectoV1 bpr1: %d bpr2: %d\n", bits_per_row[1], bits_per_row[5]);
            //fprintf(stdout, "Button        = %d\n", bb[1][1]&0x10 ? 1 : 0);

            if (wind) {
                // Wind sensor
                int skip = -1;
                /* Untested code written according to the specification, may not decode correctly  */
                if ((bb[1][1]&0xe) == 0x8 && bb[1][2] == 0) {
                    skip = 0;
                } else if ((bb[1][1]&0xe) == 0xe) {
                    skip = 4;
                } //According to supplied data!
                if (skip >= 0) {
                    double speed = reverse8(bb[1 + skip][3]);
                    double gust = reverse8(bb[5 + skip][3]);
                    int direction = (reverse8(bb[5 + skip][2]) << 1) | (bb[5 + skip][1] & 0x1);

                    data = data_make(
                                    "model",          "",           DATA_STRING, "AlectoV1 Wind Sensor",
                                    "id",             "House Code", DATA_INT,    sensor_id,
                                    "channel",        "Channel",    DATA_INT,    channel,
                                    "battery",        "Battery",    DATA_STRING, battery_low ? "LOW" : "OK",
                                    "wind_speed",     "Wind speed", DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, speed * 0.2F,
                                    "wind_gust",      "Wind gust",  DATA_FORMAT, "%.2f m/s", DATA_DOUBLE, gust * 0.2F,
                                    "wind_direction", "Direction",  DATA_INT,    direction,
                                    "mic",           "Integrity",   DATA_STRING,    "CHECKSUM",
                                    NULL);
                    decoder_output_data(decoder, data);
                }
            } else {
                // Rain sensor
                double rain_mm = ((reverse8(bb[1][3]) << 8)+reverse8(bb[1][2])) * 0.25F;

                data = data_make(
                                "model",         "",           DATA_STRING, "AlectoV1 Rain Sensor",
                                "id",            "House Code", DATA_INT,    sensor_id,
                                "channel",       "Channel",    DATA_INT,    channel,
                                "battery",       "Battery",    DATA_STRING, battery_low ? "LOW" : "OK",
                                "rain_total",    "Total Rain", DATA_FORMAT, "%.02f mm", DATA_DOUBLE, rain_mm,
                                "mic",           "Integrity",  DATA_STRING,    "CHECKSUM",
                                NULL);
                decoder_output_data(decoder, data);
            }
        } else if (bb[2][0] == bb[3][0] && bb[3][0] == bb[4][0] && bb[4][0] == bb[5][0] &&
                bb[5][0] == bb[6][0] && (bb[3][4] & 0xf) == 0 && (bb[5][4] & 0xf) == 0) {
            //static char * temp_states[4] = {"stable", "increasing", "decreasing", "invalid"};
            temp = (int16_t) ((uint16_t) (reverse8(bb[1][1]) >> 4) | (reverse8(bb[1][2]) << 4));
            if ((temp & 0x800) != 0) {
                temp |= 0xf000;
            }
            humidity = bcd_decode8(reverse8(bb[1][3]));
            if (humidity>100) return 0;//extra detection false positive!! prologue is also 36bits and sometimes detected as alecto

            data = data_make(
                            "model",         "",            DATA_STRING, "AlectoV1 Temperature Sensor",
                            "id",            "House Code",  DATA_INT,    sensor_id,
                            "channel",       "Channel",     DATA_INT,    channel,
                            "battery",       "Battery",     DATA_STRING, battery_low ? "LOW" : "OK",
                            "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, (float) temp / 10.0F,
                            "humidity",      "Humidity",    DATA_FORMAT, "%u %%",   DATA_INT, humidity,
                            "mic",           "Integrity",   DATA_STRING,    "CHECKSUM",
                            NULL);
            decoder_output_data(decoder, data);
        }
        if (decoder->verbose){
            fprintf(stdout, "Received Data = ");
            bitrow_print(bb[1], 40);
            if (wind) {
                fprintf(stdout, "Rcvd Data 2   = ");
                bitrow_print(bb[5], 40);
            }
            /*
         * fprintf(stdout, "L2M: %02x %02x %02x %02x %02x\n",reverse8(bb[1][0]),reverse8(bb[1][1]),reverse8(bb[1][2]),reverse8(bb[1][3]),reverse8(bb[1][4]));
         */
        }
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
    "humidity",
    "rain_total",
    "wind_speed",
    "wind_gust",
    "wind_direction",
    "mic",
    NULL
};

r_device alectov1 = {
    .name           = "AlectoV1 Weather Sensor (Alecto WS3500 WS4500 Ventus W155/W044 Oregon)",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 2000,
    .long_width     = 4000,
    .gap_limit      = 7000,
    .reset_limit    = 10000,
    .decode_fn      = &alectov1_callback,
    .disabled       = 0,
    .fields         = output_fields
};
