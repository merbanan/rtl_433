#include "rtl_433.h"

static int silvercrest_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS],int16_t bits_per_row[BITBUF_ROWS]) {
    /* FIXME validate the received message better */
    if (bb[1][0] == 0xF8 &&
        bb[2][0] == 0xF8 &&
        bb[3][0] == 0xF8 &&
        bb[4][0] == 0xF8 &&
        bb[1][1] == 0x4d &&
        bb[2][1] == 0x4d &&
        bb[3][1] == 0x4d &&
        bb[4][1] == 0x4d) {
        /* Pretty sure this is a Silvercrest remote */
        fprintf(stderr, "Remote button event:\n");
        fprintf(stderr, "model = Silvercrest, %d bits\n",bits_per_row[1]);
        fprintf(stderr, "%02x %02x %02x %02x %02x\n",bb[1][0],bb[0][1],bb[0][2],bb[0][3],bb[0][4]);

        if (debug_output)
            debug_callback(bb, bits_per_row);

        return 1;
    }
    return 0;
}

r_device silvercrest = {
    /* .id             = */ 3,
    /* .name           = */ "Silvercrest Remote Control",
    /* .modulation     = */ OOK_PWM_P,
    /* .short_limit    = */ 600/4,
    /* .long_limit     = */ 5000/4,
    /* .reset_limit    = */ 15000/4,
    /* .json_callback  = */ &silvercrest_callback,
};
