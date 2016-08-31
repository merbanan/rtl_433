#include "rtl_433.h"

static int steffen_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;

    if (bb[0][0]==0x00 && ((bb[1][0]&0x07)==0x07) && bb[1][0]==bb[2][0] && bb[2][0]==bb[3][0]) {

        fprintf(stdout, "Remote button event:\n");
        fprintf(stdout, "model   = Steffan Switch Transmitter, %d bits\n",bitbuffer->bits_per_row[1]);
	fprintf(stdout, "code    = %d%d%d%d%d\n", (bb[1][0]&0x80)>>7, (bb[1][0]&0x40)>>6, (bb[1][0]&0x20)>>5, (bb[1][0]&0x10)>>4, (bb[1][0]&0x08)>>3);

	if ((bb[1][2]&0x0f)==0x0e)
            fprintf(stdout, "button  = A\n");
        else if ((bb[1][2]&0x0f)==0x0d)
            fprintf(stdout, "button  = B\n");
        else if ((bb[1][2]&0x0f)==0x0b)
            fprintf(stdout, "button  = C\n");
        else if ((bb[1][2]&0x0f)==0x07)
            fprintf(stdout, "button  = D\n");
        else if ((bb[1][2]&0x0f)==0x0f)
            fprintf(stdout, "button  = ALL\n");
	else
	    fprintf(stdout, "button  = unknown\n");

	if ((bb[1][2]&0xf0)==0xf0) {
            fprintf(stdout, "state   = OFF\n");
	} else {
            fprintf(stdout, "state   = ON\n");
        }

        return 1;
    }
    return 0;
}

r_device steffen = {
    .name           = "Steffen Switch Transmitter",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 560,
    .long_limit     = 1080,
    .reset_limit    = 6000,
    .json_callback  = &steffen_callback,
    .disabled       = 1,
    .demod_arg      = 0,
};
