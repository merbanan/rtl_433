#include "rtl_433.h"
#include "util.h"

static int intertechno_callback(bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;

    // Validate that (at least) the required number of bytes are available
    int r = bitbuffer_find_repeated_row( bitbuffer, 1, 7*8 );
    
      //if (bb[1][1] == 0 && bb[1][0] != 0 && bb[1][3]==bb[2][3]) // ?
      // if(bb[0][0]==0 && bb[0][0] == 0 && bb[1][0] == 0x56)     // ?
    if ( r >= 0 && bb[r][0] == 0x56 ) {
        fprintf(stdout, "Switch event:\n");
        fprintf(stdout, "protocol       = Intertechno\n");
        fprintf(stdout, "rid            = %x\n",bb[r][0]);
        fprintf(stdout, "rid            = %x\n",bb[r][1]);
        fprintf(stdout, "rid            = %x\n",bb[r][2]);
        fprintf(stdout, "rid            = %x\n",bb[r][3]);
        fprintf(stdout, "rid            = %x\n",bb[r][4]);
        fprintf(stdout, "rid            = %x\n",bb[r][5]);
        fprintf(stdout, "rid            = %x\n",bb[r][6]);
        fprintf(stdout, "rid            = %x\n",bb[r][7]);
        fprintf(stdout, "ADDR Slave     = %i\n",bb[r][7] & 0x0f);
        fprintf(stdout, "ADDR Master    = %i\n",(bb[r][7] & 0xf0) >> 4);
        fprintf(stdout, "command        = %i\n",(bb[r][6] & 0x07));
        fprintf(stdout, "%02x %02x %02x %02x %02x\n",bb[r][0],bb[r][1],bb[r][2],bb[r][3],bb[r][4]);

        return 1;
    }
    return 0;
}

r_device intertechno = {
    .name           = "Intertechno 433",
    .modulation     = OOK_PULSE_PPM_RAW,
    .short_limit    = 400,
    .long_limit     = 1400,
    .reset_limit    = 10000,
    .json_callback  = &intertechno_callback,
    .disabled       = 1,
    .demod_arg      = 0,
};
