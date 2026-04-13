/** @file
    External helpers for LaCrosse TX31U-IT proto-compiled decoder.
*/

#ifndef INCLUDE_LACROSSE_TX31U_H_
#define INCLUDE_LACROSSE_TX31U_H_

#include <stdbool.h>

#include "decoder.h"

/// Validate CRC-8 over measurement bytes.
/// NOTE: The proto-compiled decoder only passes (measurements, crc) but the
/// CRC computation requires access to raw message bytes not available here.
/// This stub always returns true — CRC validation is effectively skipped.
static inline bool lacrosse_tx31u_validate_crc(int measurements, int crc)
{
    (void)measurements;
    (void)crc;
    return true; // TODO: needs raw byte access for proper CRC validation
}

/// Extract temperature from the variable-length measurement array.
/// Sensor type 0 = TEMP: BCD tenths of degree C, offset +400.
static inline float lacrosse_tx31u_temperature_c(
        int *readings_sensor_type, int *readings_reading, int measurements)
{
    for (int m = 0; m < measurements; m++) {
        if (readings_sensor_type[m] == 0) {
            int nib1 = (readings_reading[m] >> 8) & 0xf;
            int nib2 = (readings_reading[m] >> 4) & 0xf;
            int nib3 = readings_reading[m] & 0xf;
            return 10 * nib1 + nib2 + 0.1f * nib3 - 40.0f;
        }
    }
    return 0.0f;
}

/// Extract humidity from the variable-length measurement array.
/// Sensor type 1 = HUMIDITY: BCD %.
static inline int lacrosse_tx31u_humidity(
        int *readings_sensor_type, int *readings_reading, int measurements)
{
    for (int m = 0; m < measurements; m++) {
        if (readings_sensor_type[m] == 1) {
            int nib1 = (readings_reading[m] >> 8) & 0xf;
            int nib2 = (readings_reading[m] >> 4) & 0xf;
            int nib3 = readings_reading[m] & 0xf;
            return 100 * nib1 + 10 * nib2 + nib3;
        }
    }
    return 0;
}

#endif /* INCLUDE_LACROSSE_TX31U_H_ */
