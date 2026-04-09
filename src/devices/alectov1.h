/** @file
    Hand-written C helpers the proto_compiler does not emit from Python (checksum, validate, Wind4 row 9).
*/

#ifndef DEVICES_ALECTOV1_H_
#define DEVICES_ALECTOV1_H_

#include "decoder.h"

/** C only: proto_compiler emits expressions, not for-loops over bytes. */
static int alecto_checksum(uint8_t *b)
{
    int csum = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t tmp = reverse8(b[i]);
        csum += (tmp & 0xf) + ((tmp & 0xf0) >> 4);
    }

    csum = ((b[1] & 0x7f) == 0x6c) ? (csum + 0x7) : (0xf - csum);
    csum = reverse8((csum & 0xf) << 4);

    return (csum == (b[4] >> 4));
}

/** C only: row parity, then ``alecto_checksum`` on rows 1 and 5 + ``decoder_log`` on MIC failure. */
static inline int alectov1_validate_packet(r_device *decoder, bitbuffer_t *bitbuffer)
{
    bitrow_t *bb = bitbuffer->bb;

    if (bb[1][0] != bb[5][0] || bb[2][0] != bb[6][0] || (bb[1][4] & 0xf) != 0 || (bb[5][4] & 0xf) != 0 || bb[5][0] == 0 || bb[5][1] == 0)
        return DECODE_ABORT_EARLY;

    if (!alecto_checksum(bb[1]) || !alecto_checksum(bb[5])) {
        decoder_log(decoder, 1, __func__, "AlectoV1 Checksum/Parity error");
        return DECODE_FAIL_MIC;
    }
    return 0;
}

/** C only: row 9 is outside Rows[]; Wind4's (bitbuffer) properties are call-only in codegen. */
static inline double alectov1_Wind4_wind_max_m_s(bitbuffer_t *bitbuffer)
{
    return reverse8(bitbuffer->bb[9][3]) * 0.2;
}

/** C only: same reason as alectov1_Wind4_wind_max_m_s (row 9 + bitbuffer-only hook). */
static inline int alectov1_Wind4_wind_dir_deg(bitbuffer_t *bitbuffer)
{
    return (reverse8(bitbuffer->bb[9][2]) << 1) | (bitbuffer->bb[9][1] & 1);
}

#endif /* DEVICES_ALECTOV1_H_ */
