/* Kedsum temperature and humidity sensor (http://amzn.to/25IXeng)
   My models transmit at a bit lower freq. Around ~433.71 Mhz

   Copyright (C) 2016 John Lifsey
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 3 as
   published by the Free Software Foundation.

   Largely based on prologue, esperanza_ews, s3318p
   Frame appears to be a differently-endianed version of the esperanza

   Frame structure:
   IIIIIIII????CC++++ttttTTTThhhhHHHH?????? PP

   IIIIIIII unique id. changes on powercycle
   CC channel, 00 = ch1, 10=ch3
   ++++ low temp nibble
   tttt med temp nibble
   TTTT high temp nibble
   hhhh humidity low nibble
   HHHH humidity high nibble
*/
#include "rtl_433.h"
#include "data.h"
#include "util.h"

static int kedsum_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    data_t *data;
    char time_str[LOCAL_TIME_BUFLEN];

    // the signal should start with 15 sync pulses (empty rows)
    // require at least 5 received syncs
    if (bitbuffer->num_rows < 5
            || bitbuffer->bits_per_row[0] != 0
            || bitbuffer->bits_per_row[1] != 0
            || bitbuffer->bits_per_row[2] != 0
            || bitbuffer->bits_per_row[3] != 0
            || bitbuffer->bits_per_row[4] != 0)
        return 0;

    // the signal should have 6 repeats with a sync pulse between
    // require at least 4 received repeats
    int r = bitbuffer_find_repeated_row(bitbuffer, 4, 42);
    if (r<0 || bitbuffer->bits_per_row[r] != 42)
        return 0;

    uint8_t *b = bb[r];

    uint8_t humidity;
    uint8_t channel;
    uint16_t temperature_with_offset;
    float temperature_f;

    channel  = (uint8_t)(((b[1] & 0x0C) >> 2) + 1);
    humidity = (uint8_t)((b[3] & 0x03) << 6) | ((b[4] & 0xC0) >> 2) | ((b[3] & 0x3C) >> 2);

    uint8_t tnH, tnM, tnL;
    tnL = ((b[1] & 0x03) << 2) | ((b[2] & 0xC0) >> 6); // Low temp nibble
    tnM = ((b[2] & 0x3C) >> 2);                        // Med temp nibble
    tnH = ((b[2] & 0x03) << 2) | ((b[3] & 0xC0) >> 6); // high temp nibble

    temperature_with_offset =  (tnH<<8) | (tnM<<4) | tnL;
    temperature_f = (temperature_with_offset - 900) / 10.0;

    if (debug_output) {
      fprintf(stdout, "Bitstream HEX        = %02x %02x %02x %02x %02x %02x\n",b[0],b[1],b[2],b[3],b[4],b[5]);
      fprintf(stdout, "Humidity HEX         = %02x\n", b[3]);
      fprintf(stdout, "Humidity DEC         = %u\n",   humidity);
      fprintf(stdout, "Channel HEX          = %02x\n", b[1]);
      fprintf(stdout, "Channel              = %u\n",   channel);
      fprintf(stdout, "temp_with_offset HEX = %02x\n", temperature_with_offset);
      fprintf(stdout, "temp_with_offset     = %d\n",   temperature_with_offset);
      fprintf(stdout, "TemperatureF         = %.1f\n", temperature_f);
    }

    local_time_str(0, time_str);
    data = data_make("time",          "",            DATA_STRING, time_str,
                     "model",         "",            DATA_STRING, "Kedsum Temperature & Humidity Sensor",
                     "channel",       "Channel",     DATA_INT, channel,
                     "temperature_F", "Temperature", DATA_FORMAT, "%.02f F", DATA_DOUBLE, temperature_f,
                     "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
                      NULL);

    data_acquired_handler(data);
    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "channel",
    "temperature_F",
    "humidity",
    NULL
};

r_device kedsum = {
    .name           = "Kedsum Temperature & Humidity Sensor",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 2800,
    .long_limit     = 4400,
    .reset_limit    = 9000,
    .json_callback  = &kedsum_callback,
    .disabled       = 0,
    .demod_arg      = 0,
    .fields         = output_fields
};
