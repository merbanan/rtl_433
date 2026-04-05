/** @file
    Helpers for generated akhan_100F14 decoder.
*/

#ifndef DEVICES_AKHAN_100F14_H_
#define DEVICES_AKHAN_100F14_H_

#include <stdint.h>

/** Human-readable label for the 4-bit command nibble (after bit inversion). */
static inline char const *akhan_100F14_data_str(int cmd)
{
    switch (cmd) {
    case 0x1:
        return "0x1 (Lock)";
    case 0x2:
        return "0x2 (Unlock)";
    case 0x4:
        return "0x4 (Mute)";
    case 0x8:
        return "0x8 (Alarm)";
    default:
        return NULL;
    }
}

#endif /* DEVICES_AKHAN_100F14_H_ */
