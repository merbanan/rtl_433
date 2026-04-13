/** @file
    External helpers for Honeywell ActivLink (OOK) proto-compiled decoder.
*/

#ifndef INCLUDE_HONEYWELL_WDB_H_
#define INCLUDE_HONEYWELL_WDB_H_

#include <stdbool.h>

#include "decoder.h"

/// Parity validation over the 6-byte packet. Returns true if even parity.
static inline bool honeywell_wdb_validate_packet(bitbuffer_t *bitbuffer)
{
    int row = bitbuffer_find_repeated_row(bitbuffer, 4, 48);
    if (row < 0)
        return false;
    uint8_t *bytes = bitbuffer->bb[row];

    if ((!bytes[0] && !bytes[2] && !bytes[4] && !bytes[5])
            || (bytes[0] == 0xff && bytes[2] == 0xff && bytes[4] == 0xff && bytes[5] == 0xff))
        return false;

    return parity_bytes(bytes, 6) == 0;
}

/// Map device class field to string.
static inline char const *honeywell_wdb_subtype(int class_raw)
{
    switch (class_raw) {
    case 0x1: return "PIR-Motion";
    case 0x2: return "Doorbell";
    default:  return "Unknown";
    }
}

/// Map alert field to string.
static inline char const *honeywell_wdb_alert(int alert_raw)
{
    switch (alert_raw) {
    case 0x0: return "Normal";
    case 0x1:
    case 0x2: return "High";
    case 0x3: return "Full";
    default:  return "Unknown";
    }
}

#endif /* INCLUDE_HONEYWELL_WDB_H_ */
