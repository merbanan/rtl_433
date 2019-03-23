/* Example of a generic remote using PT2260/PT2262 SC2260/SC2262 EV1527 protocol
 *
 * fixed bit width of 1445 us
 * short pulse is 357 us (1/4th)
 * long pulse is 1064 (3/4th)
 * a packet is 15 pulses, the last pulse (short) is sync pulse
 * packet gap is 11.5 ms
 *
 * note that this decoder uses:
 * short-short (1 1 by the demod) as 0 (per protocol),
 * short-long (1 0 by the demod) as 1 (F per protocol),
 * long-long (0 0 by the demod) not used (1 per protocol).
 */
#include "decoder.h"

static int waveman_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[0];
    uint8_t nb[3] = {0}; // maps a pair of bits to two states, 1 0 -> 1 and 1 1 -> 0
    char id_str[2];
    int i;

    /* @todo iterate through all rows */

    /* Reject codes of wrong length */
    if (25 != bitbuffer->bits_per_row[0])
      return 0;

    /*
     * Catch the case triggering false positive for other transmitters.
     * example: Brennstuhl RCS 2044SN
     * @todo is this message valid at all??? if not then put more validation below
     *       instead of this special case
     */
    if (0xFF == b[0] &&
            0xFF == b[1] &&
            0xFF == b[2])
        return 0;

    /* Test if the bit stream has every even bit set to one */
    if (((b[0] & 0xaa) != 0xaa) || ((b[1] & 0xaa) != 0xaa) || ((b[2] & 0xaa) != 0xaa))
        return 0;

    /* Extract data from the bit stream */
    for (i = 0; i < 3; ++i) {
        nb[i] = ((b[i] & 0x40) ? 0x00 : 0x01)
                | ((b[i] & 0x10) ? 0x00 : 0x02)
                | ((b[i] & 0x04) ? 0x00 : 0x04)
                | ((b[i] & 0x01) ? 0x00 : 0x08);
    }

    id_str[0] = 'A' + nb[0];
    id_str[1] = '\0';

    data = data_make(
        "model",    "",     DATA_STRING,    _X("Waveman-Switch","Waveman Switch Transmitter"),
        "id",       "",     DATA_STRING,    id_str,
        "channel",  "",     DATA_INT,       (nb[1] >> 2) + 1,
        "button",   "",     DATA_INT,       (nb[1] & 3) + 1,
        "state",    "",     DATA_STRING,    (nb[2] == 0xe) ? "on" : "off",
        NULL);
    decoder_output_data(decoder, data);

    return 1;
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
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 357,
    .long_width     = 1064,
    .gap_limit      = 1400,
    .reset_limit    = 12000,
    .sync_width     = 0,    // No sync bit used
    .tolerance      = 200,  // us
    .decode_fn      = &waveman_callback,
    .disabled       = 0,
    .fields         = output_fields
};
