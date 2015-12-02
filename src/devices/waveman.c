#include "rtl_433.h"

static int waveman_callback(bitbuffer_t *bitbuffer) {
    uint8_t *b = bitbuffer->bb[0];
    /* Two bits map to 2 states, 0 1 -> 0 and 1 1 -> 1 */
    int i;
    uint8_t nb[3] = {0};
    data_t *data;
    char id_str[2];

    /* @todo iterate through all rows */

    /* Reject codes of wrong length */
    if ( 24 != bitbuffer->bits_per_row[0])
      return 0;

    /*
     * Catch the case triggering false positive for other transmitters.
     * example: Brennstuhl RCS 2044SN
     * @todo is this message valid at all??? if not then put more validation below
     *       instead of this special case
     */
    if ( 0xFF == b[0] &&
         0xFF == b[1] &&
         0xFF == b[2] )
        return 0;

    /* Test if the bit stream conforms to the rule of every odd bit being set to one */
    if (((b[0]&0x55)==0x55) && ((b[1]&0x55)==0x55) && ((b[2]&0x55)==0x55) && ((b[3]&0x55)==0x00)) {
        /* Extract data from the bit stream */
        for (i=0 ; i<3 ; i++) {
            nb[i] |= ((b[i]&0xC0)==0xC0) ? 0x00 : 0x01;
            nb[i] |= ((b[i]&0x30)==0x30) ? 0x00 : 0x02;
            nb[i] |= ((b[i]&0x0C)==0x0C) ? 0x00 : 0x04;
            nb[i] |= ((b[i]&0x03)==0x03) ? 0x00 : 0x08;
        }

	id_str[0] = 'A'+nb[0];
	id_str[1] = 0;
	data = data_make("model", NULL,   DATA_STRING, "Waveman Switch Transmitter",
		         "id", NULL,      DATA_STRING, id_str,
		         "channel", NULL, DATA_INT, (nb[1]>>2)+1,
		         "button", NULL,  DATA_INT, (nb[1]&3)+1,
		         "state", NULL,   DATA_STRING, (nb[2]==0xe) ? "on" : "off",
		         NULL);
	data_acquired_handler(data);

        return 1;
    }
    return 0;
}

static char *output_fields[] = {
	"model",
	"id",
	"channel",
	"button",
	"state",
	NULL
};


r_device waveman = {
    .name           = "Waveman Switch Transmitter",
    .modulation     = OOK_PULSE_PWM_RAW,
    .short_limit    = 1000/4,
    .long_limit     = 8000/4,
    .reset_limit    = 30000/4,
    .json_callback  = &waveman_callback,
    .disabled       = 0,
    .demod_arg      = 1,	// Remove startbit
    .fields         = output_fields
};
