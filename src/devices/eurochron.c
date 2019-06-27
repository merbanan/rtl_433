/* Eurochron weather station
 * (c) 2019 by Oliver Weyhmüller
 *
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Datagram format:
 *
 * IIIIIIII B00P0000 HHHHHHHH TTTTTTTT TTTT
 *
 *    I: ID (new ID will be generated at battery change!)
 *    B: Battery low
 *    P: TX-Button pressed
 *    H: Humidity (%)
 *    T: Temperature (°C * 10)
 *    0: Unknown / always zero
 *
 * Device type identification is only possible by datagram length
 * and some zero bits. Therefore this device is disabled 
 * by default (as it could easily trigger false alarms).
 */

#include "decoder.h"

static int eurochron_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int row;
    uint8_t *b;
    int temp_raw, humidity, device, battery_low, key;
    float temp_c;

    row = bitbuffer_find_repeated_row(bitbuffer, 3, 36);
    if (row < 0) // Rows identical?
        return 0;

    if (bitbuffer->bits_per_row[row] > 36) // 36 bits per row?
        return 0;

    b = bitbuffer->bb[row];

    if(b[1] & 0x0F) // lower nibble of second byte is always 0!
      return 0;

    device = b[0];

    temp_raw = (b[3] << 4) | (b[4] >> 4);
    if(temp_raw & 0x0800) // Negative value?
    { temp_raw ^= 0x0FFF;
      temp_raw *= -1;
    }
    temp_c  = (float)temp_raw / 10; // 1/10

    humidity = b[2];

    if(b[1] & 0x80) // Battery is low?
      battery_low = -1;
    else
      battery_low = 0;

    if(b[1] & 0x10) // TX-Key pressed?
      key = -1;
    else
      key = 0;

    data = data_make(
            "model", "", DATA_STRING, _X("Eurochron","Eurochron"),
            "id", "", DATA_INT, device,
            "temperature_C", "Temperature", DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
            "humidity","Humidity", DATA_INT, humidity,
            "battery", "Battery", DATA_STRING, battery_low ? "LOW" : "OK",
            "key", "Key", DATA_STRING, key ? "PRESSED" : "RELEASED",
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *eurochron_csv_output_fields[] = {
    "model",
    "id",
    "temperature_C",
    "humidity",
    "battery",
    "key",
    NULL
};

r_device eurochron = {
    .name          = "Eurochron (temperature and humidity sensor)",
    .modulation    = OOK_PULSE_PPM,
    .short_width   = 1016,
    .long_width    = 2024,
    .gap_limit     = 2100,
    .reset_limit   = 8200,
    .decode_fn     = &eurochron_callback,
    .disabled      = 1,
    .fields        = eurochron_csv_output_fields,
};
