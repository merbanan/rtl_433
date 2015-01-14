#include "rtl_433.h"

static int steffen_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS],int16_t bits_per_row[BITBUF_ROWS]) {

    if (bb[0][0]==0x00 && ((bb[1][0]&0x07)==0x07) && bb[1][0]==bb[2][0] && bb[2][0]==bb[3][0]) {

        fprintf(stderr, "Remote button event:\n");
        fprintf(stderr, "model   = Steffan Switch Transmitter, %d bits\n",bits_per_row[1]);
	fprintf(stderr, "code    = %d%d%d%d%d\n", (bb[1][0]&0x80)>>7, (bb[1][0]&0x40)>>6, (bb[1][0]&0x20)>>5, (bb[1][0]&0x10)>>4, (bb[1][0]&0x08)>>3);

	if ((bb[1][2]&0x0f)==0x0e)
            fprintf(stderr, "button  = A\n");
        else if ((bb[1][2]&0x0f)==0x0d)
            fprintf(stderr, "button  = B\n");
        else if ((bb[1][2]&0x0f)==0x0b)
            fprintf(stderr, "button  = C\n");
        else if ((bb[1][2]&0x0f)==0x07)
            fprintf(stderr, "button  = D\n");
        else if ((bb[1][2]&0x0f)==0x0f)
            fprintf(stderr, "button  = ALL\n");
	else
	    fprintf(stderr, "button  = unknown\n");

	if ((bb[1][2]&0xf0)==0xf0) {
            fprintf(stderr, "state   = OFF\n");
	} else {
            fprintf(stderr, "state   = ON\n");
        }

        if (debug_output)
            debug_callback(bb, bits_per_row);

        return 1;
    }
    return 0;
}

r_device steffen = {
    /* .id             = */ 9,
    /* .name           = */ "Steffen Switch Transmitter",
    /* .modulation     = */ OOK_PWM_D,
    /* .short_limit    = */ 140,
    /* .long_limit     = */ 270,
    /* .reset_limit    = */ 1500,
    /* .json_callback  = */ &steffen_callback,
};
