/** @file
    External helpers for Acurite 01185M proto-compiled decoder.
*/

#ifndef INCLUDE_ACURITE_01185M_H_
#define INCLUDE_ACURITE_01185M_H_

#include "decoder.h"

/// Validate add-with-carry checksum: sum of first 6 bytes vs byte 7.
static inline bool acurite_01185m_validate_checksum(
        int data_id, int data_battery_low, int data_mid,
        int data_channel, int data_temp1_raw, int data_temp2_raw,
        int data_checksum)
{
    // Reconstruct the raw bytes from the parsed fields.
    uint8_t b[7];
    b[0] = (uint8_t)data_id;
    b[1] = (uint8_t)((data_battery_low << 7) | (data_mid << 4) | data_channel);
    b[2] = (uint8_t)(data_temp1_raw >> 8);
    b[3] = (uint8_t)(data_temp1_raw & 0xff);
    b[4] = (uint8_t)(data_temp2_raw >> 8);
    b[5] = (uint8_t)(data_temp2_raw & 0xff);
    b[6] = (uint8_t)data_checksum;

    int sum = add_bytes(b, 6);
    if ((sum & 0xff) != b[6])
        return false;
    if (sum == 0)
        return false;
    return true;
}

#endif /* INCLUDE_ACURITE_01185M_H_ */
