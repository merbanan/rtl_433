#include "rtl_433.h"

static int newkaku_callback(bitbuffer_t *bitbuffer) {
    /* Two bits map to 2 states, 0 1 -> 0 and 1 1 -> 1 */
    /* Status bit can be 1 1 -> 1 which indicates DIM value. 4 extra bits are present with value */
    /*start pulse: 1T high, 10.44T low */
    /*- 26 bit:  Address */
    /*- 1  bit:  group bit*/
    /*- 1  bit:  Status bit on/off/[dim]*/
    /*- 4  bit:  unit*/
    /*- [4 bit:  dim level. Present if [dim] is used, but might be present anyway...]*/
    /*- stop pulse: 1T high, 40T low */
    bitrow_t *bb = bitbuffer->bb;
    int i;
    uint8_t tmp = 0;
    uint8_t unit = 0;
    uint8_t packet = 0;
    uint8_t bitcount = 0;
    uint32_t kakuid = 0;

    if (bb[0][0] == 0xac) {//allways starts with ac
        // first bit is from startbit sequence, not part of payload!
        // check protocol if value is 10 or 01, else stop processing as it is no vallid KAKU packet!
        //get id=24bits, remember 1st 1 bit = startbit, no payload!
        for (packet = 0; packet < 6; packet++) {//get first part kakuid
            tmp = bb[0][packet] << 1;
            if ((bb[0][packet + 1]&(1 << 7)) != 0) {// if set add bit to current
                tmp++;
            }

            for (bitcount = 0; bitcount < 8; bitcount += 2) {//process bitstream, check protocol!

                if (((tmp << bitcount & (0x80)) == 0x80)&((tmp << bitcount & (0x40)) == 0)) {
                    //add 1
                    kakuid = kakuid << 1;
                    kakuid++;
                } else
                    if (((tmp << bitcount & (0x80)) == 0)&((tmp << bitcount & (0x40)) == 0x40)) {
                    kakuid = kakuid << 1;
                    //add 0
                } else {
                    return 0; //00 and 11 indicates packet error. Do exit, no valid packet
                }
            }
        }
        tmp = bb[0][6] << 1; //Get last part ID
        for (bitcount = 0; bitcount < 4; bitcount += 2) {
            if (((tmp << bitcount & (0x80)) == 0x80)&((tmp << bitcount & (0x40)) == 0)) {
                //add 1
                kakuid = kakuid << 1;
                kakuid++;
            } else
                if (((tmp << bitcount & (0x80)) == 0)&((tmp << bitcount & (0x40)) == 0x40)) {
                //= add bit on kakuid
                kakuid = kakuid << 1;
                //add 0
            } else {
                //fprintf(stdout, " Packet error, no newkaku!!\n", tmp << bitcount);
                return 0; //00 and 11 indicates packet error. no valid packet! do exit
            }
        }
        //Get unit ID
        tmp = bb[0][7] << 1;
        if ((bb[0][8]&(1 << 7)) != 0) {// if set add bit to current
            tmp++;
        }
        for (bitcount = 0; bitcount < 8; bitcount += 2) {//process bitstream, check protocol!
            if (((tmp << bitcount & (0x80)) == 0x80)&((tmp << bitcount & (0x40)) == 0)) {
                //add 1
                unit = unit << 1;
                unit++;
            } else
                if (((tmp << bitcount & (0x80)) == 0)&((tmp << bitcount & (0x40)) == 0x40)) {
                unit = unit << 1;
                //add 0
            } else {
                return 0; //00 and 11 indicates packet error. Do exit, no valid packet
            }
        }
        fprintf(stdout, "NewKaku event:\n");
        fprintf(stdout, "Model      = NewKaKu on/off/dimmer switch\n");
        fprintf(stdout, "KakuId     = %d (H%.2X)\n", kakuid, kakuid);
        fprintf(stdout, "Unit       = %d (H%.2X)\n", unit, unit);
        fprintf(stdout, "Group Call = %s\n", (((bb[0][6] & (0x04)) == 0x04)&((bb[0][6] & (0x02)) == 0)) ? "Yes" : "No");
        fprintf(stdout, "Command    = %s\n", (((bb[0][6] & (0x01)) == 0x01)&((bb[0][7] & (0x80)) == 0)) ? "On" : "Off");
        if (((bb[0][6] & (0x01)) == 0x01)&((bb[0][7] & (0x80)) == 0x80)) {//11 indicates DIM command, 4 extra bits indicate DIM value
            fprintf(stdout, "Dim        = Yes\n");
            tmp = bb[0][8] << 1; // get packet, loose first bit
            uint8_t dv = 0;
            if ((bb[0][9]&(1 << 7)) != 0) {// if bit is set Add to current packet
                tmp++;
                for (bitcount = 0; bitcount < 8; bitcount += 2) {//process last bit outside
                    if (((tmp << bitcount & (0x80)) == 0x80)&((tmp << bitcount & (0x40)) == 0)) {
                        //add 1
                        dv = dv << 1;
                        dv++;
                    } else
                        if (((tmp << bitcount & (0x80)) == 0)&((tmp << bitcount & (0x40)) == 0x40)) {
                        dv = dv << 1;
                        //add 0
                    } else {
                        return 0; //00 and 11 indicates packet error. Do exit, no valid packet
                    }
                }
            }
            fprintf(stdout, "Dim Value  = %d\n", dv);
        } else {
            fprintf(stdout, "Dim        = No\n");
            fprintf(stdout, "Dim Value  = 0\n");
        }
        fprintf(stdout, "%02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                bb[0][0], bb[0][1], bb[0][2], bb[0][3], bb[0][4], bb[0][5], bb[0][6], bb[0][7], bb[0][8]);
        return 1;
    }
    return 0;
}

r_device newkaku = {
    .name           = "KlikAanKlikUit Wireless Switch",
    .modulation     = OOK_PWM_D,
    .short_limit    = 200,
    .long_limit     = 800,
    .reset_limit    = 4000,
    .json_callback  = &newkaku_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};
