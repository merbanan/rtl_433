#include "rtl_433.h"
#include "data.h"
#include "util.h"

/* Conrad Electronics S3318P outdoor sensor
 *
 * Transmit Interval: every ~50s
 * Message Format: 40 bits (10 nibbles)
 *
 *
 * Nibble:    1   2    3   4    5   6    7   8    9   10
 * Type:   PP IIIIIIII ??CCTTTT TTTTTTTT HHHHHHHH XB?????? PP
 * BIT/8   00 01234567 01234567 01234567 01234567 01234567 00
 * BIT/A   00 01234567 89012345 57890123 45678901 23456789 00
 *            0          1          2          3
 * I = sensor ID (changes on battery change)
 * C = channel number
 * T = temperature
 * H = humidity
 * X = tx-button pressed
 * B = low battery
 * P = Pre-/Postamble
 * ? = unknown meaning
 *
 *
 * [01] {42} 04 15 66 e2 a1 00 : 00000100 00010101 01100110 11100010 10100001 00 ---> Temp/Hum/Ch:23.2/46/1
 *
 * Temperature:
 * Sensor sends data in °F, lowest supported value is 90°F
 * 12 bit uingned and scaled by 10 (Nibbles: 6,5,4)
 * in this case "011001100101" =  1637/10 - 90 = 73.7 °F (23.17 °C)
 *
 * Humidity:
 * 8 bit unsigned (Nibbles 8,7)
 * in this case "00101110" = 46
 *
 * Channel number: (Bits 10,11) + 1
 * in this case "00" --> "00" +1 = Channel1
 *
 * Battery status: (Bit 33) (0 normal, 1 voltage is below ~2.7 V)
 * TX-Button: (Bit 32) (0 indicates regular transmission, 1 indicates requested by pushbutton)
 *
 * Rolling Code / Device ID: (Nibble 1)
 * changes on every battery change
 *
 * Unknown1: (Bits 8,9) changes not so often
 * Unknown2: (Bits 36-39) changes with every packet, probably checksum
 * Unknown3: (Bits 34,35) changes not so often, mayby also part of the checksum
 *
 */


static int s3318p_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    /* Get time now */
    local_time_str(0, time_str);

    /* Reject codes of wrong length */
    if ( 42 != bitbuffer->bits_per_row[1])
      return 0;

    /* shift all the bits left 2 to align the fields */
    int i;
    for (i = 0; i < BITBUF_COLS-1; i++) {
      uint8_t bits1 = bb[1][i] << 2;
      uint8_t bits2 = (bb[1][i+1] & 0xC0) >> 6;
      bits1 |= bits2;
      bb[1][i] = bits1;
    }

    uint8_t humidity;
    uint8_t button;
    uint8_t battery_low;
    uint8_t channel;
    uint8_t sensor_id;
    uint16_t temperature_with_offset;
    float temperature_f;

    /* IIIIIIII ??CCTTTT TTTTTTTT HHHHHHHH XB?????? PP */
    humidity = (uint8_t)(((bb[1][3] & 0x0F) << 4) | ((bb[1][3] & 0xF0) >> 4));
    button = (uint8_t)(bb[1][4] >> 7);
    battery_low = (uint8_t)((bb[1][4] & 0x40) >> 6);
    channel = (uint8_t)(((bb[1][1] & 0x30) >> 4) + 1);
    sensor_id = (uint8_t)(bb[1][0]);

    temperature_with_offset = ((bb[1][2] & 0x0F) << 8) | (bb[1][2] & 0xF0) | (bb[1][1] & 0x0F);
    temperature_f = (temperature_with_offset - 900) / 10.0;

    if (debug_output) {
      bitbuffer_print(bitbuffer);
      fprintf(stderr, "Sensor ID            = %2x\n",  sensor_id);
      fprintf(stdout, "Bitstream HEX        = %02x %02x %02x %02x %02x %02x\n",bb[1][0],bb[1][1],bb[1][2],bb[1][3],bb[1][4],bb[1][5]);
      fprintf(stdout, "Humidity HEX         = %02x\n", bb[1][3]);
      fprintf(stdout, "Humidity DEC         = %u\n",   humidity);
      fprintf(stdout, "Button               = %d\n",   button);
      fprintf(stdout, "Battery Low          = %d\n",   battery_low);
      fprintf(stdout, "Channel HEX          = %02x\n", bb[1][1]);
      fprintf(stdout, "Channel              = %u\n",   channel);
      fprintf(stdout, "temp_with_offset HEX = %02x\n", temperature_with_offset);
      fprintf(stdout, "temp_with_offset     = %d\n",   temperature_with_offset);
      fprintf(stdout, "TemperatureF         = %.1f\n", temperature_f);
    }

    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "S3318P Temperature & Humidity Sensor",
                     "id",            "House Code",  DATA_INT, sensor_id,
                     "channel",       "Channel",     DATA_INT, channel,
                     "battery",       "Battery",     DATA_STRING, battery_low ? "LOW" : "OK",
                     "button",        "Button",      DATA_INT, button,
                     "temperature_F", "Temperature", DATA_FORMAT, "%.02f F", DATA_DOUBLE, temperature_f,
                     "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                      NULL);

    data_acquired_handler(data);

    return 0;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "channel",
    "battery",
    "button",
    "temperature_C",
    "humidity",
    NULL
};


r_device s3318p = {
    .name           = "S3318P Temperature & Humidity Sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 2800,
    .long_limit     = 4400,
    .reset_limit    = 8000,
    .json_callback  = &s3318p_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};

