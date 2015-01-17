#include "rtl_433.h"

static int intertechno_callback(uint8_t bb[BITBUF_ROWS][BITBUF_COLS], int16_t bits_per_row[BITBUF_ROWS]) {

      //if (bb[1][1] == 0 && bb[1][0] != 0 && bb[1][3]==bb[2][3]){
      if(bb[0][0]==0 && bb[0][0] == 0 && bb[1][0] == 0x56){
        fprintf(stderr, "Switch event:\n");
        fprintf(stderr, "protocol       = Intertechno\n");
        fprintf(stderr, "rid            = %x\n",bb[1][0]);
        fprintf(stderr, "rid            = %x\n",bb[1][1]);
        fprintf(stderr, "rid            = %x\n",bb[1][2]);
        fprintf(stderr, "rid            = %x\n",bb[1][3]);
        fprintf(stderr, "rid            = %x\n",bb[1][4]);
        fprintf(stderr, "rid            = %x\n",bb[1][5]);
        fprintf(stderr, "rid            = %x\n",bb[1][6]);
        fprintf(stderr, "rid            = %x\n",bb[1][7]);
        fprintf(stderr, "ADDR Slave     = %i\n",bb[1][7] & 0b00001111);
        fprintf(stderr, "ADDR Master    = %i\n",(bb[1][7] & 0b11110000) >> 4);
	fprintf(stderr, "command        = %i\n",(bb[1][6] & 0b00000111));
        fprintf(stderr, "%02x %02x %02x %02x %02x\n",bb[1][0],bb[1][1],bb[1][2],bb[1][3],bb[1][4]);

        if (debug_output)
            debug_callback(bb, bits_per_row);

        return 1;
    }
    return 0;
}

r_device intertechno = {
    /* .id             = */ 11,
    /* .name           = */ "Intertechno 433",
    /* .modulation     = */ OOK_PWM_D,
    /* .short_limit    = */ 100,
    /* .long_limit     = */ 350,
    /* .reset_limit    = */ 3000,
    /* .json_callback  = */ &intertechno_callback,
    /* .json_callback  = */ //&debug_callback,
};
