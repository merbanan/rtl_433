#include "rtl_433.h"
#include "util.h"

/* Documentation also at http://www.tfd.hu/tfdhu/files/wsprotocol/auriol_protocol_v20.pdf
 * Message Format: (9 nibbles, 36 bits):
 * Please note that bytes need to be reversed before processing!
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
 *********************************************************************************************
 */

uint8_t reverse8(uint8_t x) {
    x = (x & 0xF0) >> 4 | (x & 0x0F) << 4;
    x = (x & 0xCC) >> 2 | (x & 0x33) << 2;
    x = (x & 0xAA) >> 1 | (x & 0x55) << 1;
    return x;
}

uint8_t bcd_decode8(uint8_t x) {
    return ((x & 0xF0) >> 4) * 10 + (x & 0x0F);
}

static int alectov1_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    int temperature_before_dec;
    int temperature_after_dec;
    int16_t temp;
    uint8_t humidity, csum = 0, csum2 = 0;
    int i;
    time_t time_now;
    char time_str[LOCAL_TIME_BUFLEN];
    if (bb[1][0] == bb[5][0] && bb[2][0] == bb[6][0] && (bb[1][4] & 0xf) == 0 && (bb[5][4] & 0xf) == 0
            && (bb[5][0] != 0 && bb[5][1] != 0)) {

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
        /* Quit if checksup does not work out */
        if (csum != (bb[1][4] >> 4) || csum2 != (bb[5][4] >> 4)) {
            //fprintf(stdout, "\nAlectoV1 CRC error");
            time(&time_now);
            local_time_str(time_now, time_str);
            fprintf(stdout,
            "%s AlectoV1 Checksum/Parity error\n",
            time_str);
            return 0;
        } //Invalid checksum


        uint8_t wind = 0;

        if ((bb[1][1] & 0xe0) == 0x60) {
            wind = ((bb[1][1] & 0xf) == 0xc) ? 0 : 1;
            time(&time_now);
            local_time_str(time_now, time_str);
            time(&time_now);
            local_time_str(time_now, time_str);
            fprintf(stdout,
            "%s AlectoV1 %s Sensor %d",
            time_str,
            wind ? "Wind" : "Rain",
            reverse8(bb[1][0])
            );
            //left out data (not needed):
            //bb[1][1]&0x10 ? "timed event":"Button generated ");
            //fprintf(stdout, "Protocol      = AlectoV1 bpr1: %d bpr2: %d\n", bits_per_row[1], bits_per_row[5]);
            //fprintf(stdout, "Button        = %d\n", bb[1][1]&0x10 ? 1 : 0);
            if (wind) {
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
                    fprintf(stdout, ": Wind speed %.0f units = %.2f m/s", speed, speed * 0.2);
                    fprintf(stdout, ": Wind gust %.0f units = %.2f m/s", gust, gust * 0.2);
                    fprintf(stdout, ": Direction %.2i degrees", direction);
                    fprintf(stdout, ": Battery %s\n", bb[1][1]&0x80 ? "Low" : "OK");
                }
            } else {
                
                double rain_mm = ((reverse8(bb[1][3]) << 8)+reverse8(bb[1][2])) * 0.25;
                unsigned int rain1=0;
                rain1 = (reverse8(bb[1][3]) << 8)+reverse8(bb[1][2]);
                fprintf(stdout, ": Rain %.02f mm/m2", rain_mm);
                fprintf(stdout, ": Battery %s\n", bb[1][1]&0x80 ? "Low" : "OK");
            }
        } else if (bb[2][0] == bb[3][0] && bb[3][0] == bb[4][0] && bb[4][0] == bb[5][0] &&
                bb[5][0] == bb[6][0] && (bb[3][4] & 0xf) == 0 && (bb[5][4] & 0xf) == 0) {
            //static char * temp_states[4] = {"stable", "increasing", "decreasing", "invalid"};
            temp = (int16_t) ((uint16_t) (reverse8(bb[1][1]) >> 4) | (reverse8(bb[1][2]) << 4));
            if ((temp & 0x800) != 0) {
                temp |= 0xf000;
            }
            temperature_before_dec = abs(temp / 10);
            temperature_after_dec = abs(temp % 10);
            humidity = bcd_decode8(reverse8(bb[1][3]));
            time(&time_now);
            local_time_str(time_now, time_str);
            fprintf(stdout,
            "%s AlectoV1 Sensor %d Channel %d",
            time_str,
            reverse8(bb[1][0]),
            (bb[1][0] & 0xc) >> 2
            );
            fprintf(stdout, ": Temperature %s%d.%d C", temp < 0 ? "-" : "", temperature_before_dec, temperature_after_dec);
            fprintf(stdout, ": Humidity %d %%", humidity);
            fprintf(stdout, ": Battery %s\n", bb[1][1]&0x80 ? "Low" : "OK");
            
        }        
        if (debug_output){
           fprintf(stdout, "Checksum      = %01x (calculated %01x)\n", bb[1][4] >> 4, csum);
           fprintf(stdout, "Received Data = %02x %02x %02x %02x %02x\n", bb[1][0], bb[1][1], bb[1][2], bb[1][3], bb[1][4]);
           if (wind) fprintf(stdout, "Rcvd Data 2   = %02x %02x %02x %02x %02x\n", bb[5][0], bb[5][1], bb[5][2], bb[5][3], bb[5][4]);
         /*
         * fprintf(stdout, "L2M: %02x %02x %02x %02x %02x\n",reverse8(bb[1][0]),reverse8(bb[1][1]),reverse8(bb[1][2]),reverse8(bb[1][3]),reverse8(bb[1][4]));
         */
        }
        return 1;
    }
    return 0;
}

//Timing based on 250000
r_device alectov1 = {
    .name           = "AlectoV1 Weather Sensor (Alecto WS3500 WS4500 Ventus W155/W044 Oregon)",
    .modulation     = OOK_PWM_D,
    .short_limit    = 3500 / 4, //875
    .long_limit     = 7000 / 4, //1750
    .reset_limit    = 15000 / 4, //3750
    .json_callback  = &alectov1_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};
