/* Bresser sensor protocol
 *
 * The protocol is for the wireless Temperature/Humidity sensor
 * Bresser Thermo-/Hygro-Sensor 3CH
 * also works for Renkforce DM-7511
 *
 * The sensor sends 15 identical packages of 40 bits each ~60s.
 * The bits are PWM modulated with On Off Keying.
 *
 * A short pulse of 250 us followed by a 500 us gap is a 0 bit,
 * a long pulse of 500 us followed by a 250 us gap is a 1 bit,
 * there is a sync preamble of pulse, gap, 750 us each, repeated 4 times.
 * Actual received and demodulated timings might be 2% shorter.
 *
 * The data is grouped in 5 bytes / 10 nibbles
 * [id] [id] [flags] [temp] [temp] [temp] [humi] [humi] [chk] [chk]
 *
 * id is an 8 bit random id that is generated when the sensor starts
 * flags are 4 bits battery low indicator, test button press and channel
 * temp is 12 bit unsigned fahrenheit offset by 90 and scaled by 10
 * humi is 8 bit relative humidity percentage
 *
 * Copyright (C) 2015 Christian W. Zuckschwerdt <zany@triq.net>
 */
#include "decoder.h"

static int bresser_3ch_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    data_t *data;
    uint8_t *b;

    int id, status, battery_low, test, channel, temp_raw, humidity;
    float temp_f;

    int r = bitbuffer_find_repeated_row(bitbuffer, 3, 40);
    if (r < 0 || bitbuffer->bits_per_row[r] > 42) {
        return 0;
    }

    b = bitbuffer->bb[r];
    b[0] = ~b[0];
    b[1] = ~b[1];
    b[2] = ~b[2];
    b[3] = ~b[3];
    b[4] = ~b[4];

    if (((b[0] + b[1] + b[2] + b[3] - b[4]) & 0xFF) != 0) {
        if (decoder->verbose) {
            fprintf(stderr, "Bresser 3CH checksum error\n");
        }
        return 0;
    }

    id = b[0];
    status = b[1];
    battery_low = (b[1] & 0x80) >> 7;
    test = (b[1] & 0x40) >> 6;
    channel = (b[1] & 0x30) >> 4;

    temp_raw = ((b[1] & 0x0F) << 8) + b[2];
    // 12 bits allows for values -90.0 F - 319.6 F (-67 C - 159 C)
    temp_f = (temp_raw - 900) / 10.0;

    humidity = b[3];

    if ((channel == 0) || (humidity > 100) || (temp_f < -20.0) || (temp_f > 160.0)) {
        if (decoder->verbose) {
            fprintf(stderr, "Bresser 3CH data error\n");
        }
        return 0;
    }

    data = data_make(
            "model",         "",            DATA_STRING, _X("Bresser-3CH","Bresser 3CH sensor"),
            "id",            "Id",          DATA_INT,    id,
            "channel",       "Channel",     DATA_INT,    channel,
            "battery",       "Battery",     DATA_STRING, battery_low ? "LOW": "OK",
            "temperature_F", "Temperature", DATA_FORMAT, "%.2f F", DATA_DOUBLE, temp_f,
            "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "temperature_F",
    "humidity",
    "mic",
    NULL
};

r_device bresser_3ch = {
    .name           = "Bresser Thermo-/Hygro-Sensor 3CH",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 250,   // short pulse is ~250 us
    .long_width     = 500,   // long pulse is ~500 us
    .sync_width     = 750,   // sync pulse is ~750 us
    .gap_limit      = 625,   // long gap (with short pulse) is ~500 us, sync gap is ~750 us
    .reset_limit    = 1250,  // maximum gap is 1000 us (long gap + longer sync gap on last repeat)
    .decode_fn      = &bresser_3ch_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
