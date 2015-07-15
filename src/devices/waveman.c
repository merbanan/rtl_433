#include "rtl_433.h"

static int waveman_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    /* Two bits map to 2 states, 0 1 -> 0 and 1 1 -> 1 */
    int i;
    uint8_t nb[3] = {0};

    if (((bb[0][0]&0x55)==0x55) && ((bb[0][1]&0x55)==0x55) && ((bb[0][2]&0x55)==0x55) && ((bb[0][3]&0x55)==0x00)) {
        for (i=0 ; i<3 ; i++) {
            nb[i] |= ((bb[0][i]&0xC0)==0xC0) ? 0x00 : 0x01;
            nb[i] |= ((bb[0][i]&0x30)==0x30) ? 0x00 : 0x02;
            nb[i] |= ((bb[0][i]&0x0C)==0x0C) ? 0x00 : 0x04;
            nb[i] |= ((bb[0][i]&0x03)==0x03) ? 0x00 : 0x08;
        }

        fprintf(stdout, "Remote button event:\n");
        fprintf(stdout, "model   = Waveman Switch Transmitter, %d bits\n",bitbuffer->bits_per_row[1]);
        fprintf(stdout, "id      = %c\n", 'A'+nb[0]);
        fprintf(stdout, "channel = %d\n", (nb[1]>>2)+1);
        fprintf(stdout, "button  = %d\n", (nb[1]&3)+1);
        fprintf(stdout, "state   = %s\n", (nb[2]==0xe) ? "on" : "off");
        fprintf(stdout, "%02x %02x %02x\n",nb[0],nb[1],nb[2]);

        return 1;
    }
    return 0;
}

r_device waveman = {
    .name           = "Waveman Switch Transmitter",
    .modulation     = OOK_PWM_P,
    .short_limit    = 1000/4,
    .long_limit     = 8000/4,
    .reset_limit    = 30000/4,
    .json_callback  = &waveman_callback,
    .disabled       = 0,
    .demod_arg      = 0,
};
