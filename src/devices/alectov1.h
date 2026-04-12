/** @file
    External helpers for AlectoV1 proto-compiled decoder.
*/

#ifndef INCLUDE_ALECTOV1_H_
#define INCLUDE_ALECTOV1_H_

#include "decoder.h"

/// Validate checksum on rows 1 and 5 of the bitbuffer.
static inline bool alectov1_validate_packet(bitbuffer_t *bitbuffer)
{
    uint8_t *bb1 = bitbuffer->bb[1];
    uint8_t *bb5 = bitbuffer->bb[5];

    if (bb1[0] != bb5[0] || bitbuffer->bb[2][0] != bitbuffer->bb[6][0]
            || (bb1[4] & 0xf) != 0 || (bb5[4] & 0xf) != 0
            || bb5[0] == 0 || bb5[1] == 0)
        return false;

    // Nibble-sum checksum on rows 1 and 5.
    for (int r = 0; r < 2; r++) {
        uint8_t *b = r == 0 ? bb1 : bb5;
        int csum = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t tmp = reverse8(b[i]);
            csum += (tmp & 0xf) + ((tmp & 0xf0) >> 4);
        }
        csum = ((b[1] & 0x7f) == 0x6c) ? (csum + 0x7) : (0xf - csum);
        csum = reverse8((csum & 0xf) << 4);
        if (csum != (b[4] >> 4))
            return false;
    }
    return true;
}

/// Wind4 gust speed — TODO: needs bitbuffer access, stubbed to 0.
static inline float alectov1_Wind4_wind_max_m_s(void)
{
    return 0.0f; // TODO: requires bitbuffer arg not yet supported by DSL
}

/// Wind4 direction — TODO: needs bitbuffer access, stubbed to 0.
static inline int alectov1_Wind4_wind_dir_deg(void)
{
    return 0; // TODO: requires bitbuffer arg not yet supported by DSL
}

#endif /* INCLUDE_ALECTOV1_H_ */
