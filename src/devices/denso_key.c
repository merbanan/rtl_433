#include "decoder.h"

static int denso_key_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    /*
     * Early debugging aid to see demodulated bits in buffer and
     * to determine if your width settings are matched and firing
     * this callback.
     */
     fprintf(stderr,"DENSO callback was triggered :-) \n");
     bitbuffer_print(bitbuffer);
     return 1;
}

r_device denso_key = {
    .name           = "Denso key fob",
    .modulation     = FSK_PULSE_PWM,
    .short_width    = 348, // = 130 * 4
    .long_width     = 696, // = 250 * 4
    .reset_limit    = 1088,
    .decode_fn      = &denso_key_callback,
    .disabled       = 0, // stop debug output from spamming unsuspecting users
};


