#include "decoder.h"

static int newkaku_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    /* Two bits map to 2 states, 0 1 -> 0 and 1 1 -> 1 */
    /* Status bit can be 1 1 -> 1 which indicates DIM value. 4 extra bits are present with value */
    /*start pulse: 1T high, 10.44T low */
    /*- 26 bit:  Address */
    /*- 1  bit:  group bit*/
    /*- 1  bit:  Status bit on/off/[dim]*/
    /*- 4  bit:  unit*/
    /*- [4 bit:  dim level. Present if [dim] is used, but might be present anyway...]*/
    /*- stop pulse: 1T high, 40T low */
    data_t *data;
    bitrow_t *bb = bitbuffer->bb;
    uint8_t tmp = 0;
    uint8_t unit = 0;
    uint8_t packet = 0;
    uint8_t bitcount = 0;
    uint32_t kakuid = 0;
    uint8_t dv = 0;
    char *group_call, *command, *dim;

    if (bb[0][0] == 0xac || bb[0][0] == 0xb2) {//always starts with ac or b2
        // first bit is from startbit sequence, not part of payload!
        // check protocol if value is 10 or 01, else stop processing as it is no valid KAKU packet!
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
        group_call = (((bb[0][6] & (0x04)) == 0x04)&((bb[0][6] & (0x02)) == 0)) ? "Yes" : "No";
        command = (((bb[0][6] & (0x01)) == 0x01)&((bb[0][7] & (0x80)) == 0)) ? "On" : "Off";
        if (((bb[0][6] & (0x01)) == 0x01)&((bb[0][7] & (0x80)) == 0x80)) {//11 indicates DIM command, 4 extra bits indicate DIM value
            dim = "Yes";
            tmp = bb[0][8] << 1; // get packet, loose first bit

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
        } else {
            dim = "No";
        }

        data = data_make(
                         "model",         "",            DATA_STRING, _X("KlikAanKlikUit-Switch","KlikAanKlikUit Wireless Switch"),
                         "id",            "",            DATA_INT, kakuid,
                         "unit",          "Unit",        DATA_INT, unit,
                         "group_call",    "Group Call",  DATA_STRING, group_call,
                         "command",       "Command",     DATA_STRING, command,
                         "dim",           "Dim",         DATA_STRING, dim,
                         "dim_value",     "Dim Value",   DATA_INT, dv,
                         NULL);
        decoder_output_data(decoder, data);

        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "model",
    "id",
    "unit",
    "group_call",
    "command",
    "dim",
    "dim_value",
    NULL
};

r_device newkaku = {
    .name           = "KlikAanKlikUit Wireless Switch",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 300,
    .long_width     = 1400,
    .reset_limit    = 3200,
    .decode_fn      = &newkaku_callback,
    .disabled       = 0,
    .fields         = output_fields
};
