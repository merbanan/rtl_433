/* lacrosse_tx31u.h – hand-written helpers included by the generated decoder. */
#pragma once

/* validate: CRC-8 (poly 0x31, init 0x00) over the 2-byte header and all
   measurement pairs.  b[0] is the first payload byte (after preamble). */
static int lacrosse_tx31u_validate(uint8_t *b, int measurements, int crc)
{
    int expected = crc8(b, 2 + measurements * 2, 0x31, 0x00);
    return (crc == expected) ? 0 : DECODE_FAIL_MIC;
}

/* temperature_c: scan readings[] for sensor_type == 0 (TEMP).
   Encoding: three BCD nibbles with a 40 °C offset.
   Returns -999.0f if no temperature reading is present. */
static float lacrosse_tx31u_temperature_c(
        int *readings_sensor_type, int *readings_reading, int measurements)
{
    for (int i = 0; i < measurements; i++) {
        if (readings_sensor_type[i] == 0) {
            int r    = readings_reading[i];
            int nib1 = (r >> 8) & 0xF; /* tens   */
            int nib2 = (r >> 4) & 0xF; /* ones   */
            int nib3 = r & 0xF;        /* tenths */
            return 10.0f * nib1 + nib2 + 0.1f * nib3 - 40.0f;
        }
    }
    return -999.0f;
}

/* humidity: scan readings[] for sensor_type == 1 (HUMIDITY).
   Encoding: three BCD nibbles giving % RH.
   Returns -1 if no humidity reading is present. */
static int lacrosse_tx31u_humidity(
        int *readings_sensor_type, int *readings_reading, int measurements)
{
    for (int i = 0; i < measurements; i++) {
        if (readings_sensor_type[i] == 1) {
            int r    = readings_reading[i];
            int nib1 = (r >> 8) & 0xF;
            int nib2 = (r >> 4) & 0xF;
            int nib3 = r & 0xF;
            return 100 * nib1 + 10 * nib2 + nib3;
        }
    }
    return -1;
}
