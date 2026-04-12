/** @file
    External helpers for Akhan 100F14 proto-compiled decoder.
*/

#ifndef INCLUDE_AKHAN_100F14_H_
#define INCLUDE_AKHAN_100F14_H_

/// Map 4-bit command code to display string.
static inline char const *akhan_100F14_data_str(int cmd)
{
    switch (cmd) {
    case 0x1: return "0x1 (Lock)";
    case 0x2: return "0x2 (Unlock)";
    case 0x4: return "0x4 (Mute)";
    case 0x8: return "0x8 (Alarm)";
    default:  return NULL;
    }
}

#endif /* INCLUDE_AKHAN_100F14_H_ */
