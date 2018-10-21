/* Akhan remote keyless entry system
 *
 *    This RKE system uses a HS1527 OTP encoder (http://sc-tech.cn/en/hs1527.pdf)
 *    Each message consists of a preamble, 20 bit id and 4 data bits.
 *
 *    (code based on chuango.c and generic_remote.c)
 *
 * Note: simple 24 bit fixed ID protocol (x1527 style) and should be handled by the flex decoder.
 */
#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"
#include "data.h"

static int akhan_rke_callback(bitbuffer_t *bitbuffer) {
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;
    uint8_t *b;
    int id;
    int cmd;
    char *cmd_str;

    if (bitbuffer->bits_per_row[0] != 25)
        return 0;
    b = bitbuffer->bb[0];

    //invert bits, short pulse is 0, long pulse is 1
    b[0] = ~b[0];
    b[1] = ~b[1];
    b[2] = ~b[2];

    id = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
    cmd = b[2] & 0x0F;
    switch (cmd) {
        case 0x1: cmd_str = "0x1 (Lock)"; break;
        case 0x2: cmd_str = "0x2 (Unlock)"; break;
        case 0x4: cmd_str = "0x4 (Mute)"; break;
        case 0x8: cmd_str = "0x8 (Alarm)"; break;
        default:  cmd_str = NULL; break;
    }

    if (!cmd_str)
        return 0;

    local_time_str(0, time_str);
    data = data_make(
            "time",     "",             DATA_STRING, time_str,
            "model",    "",             DATA_STRING, "Akhan 100F14 remote keyless entry",
            "id",       "ID (20bit)",   DATA_FORMAT, "0x%x", DATA_INT, id,
            "data",     "Data (4bit)",  DATA_STRING, cmd_str,
            NULL);

    data_acquired_handler(data);
    return 1;
}

static char *output_fields[] = {
    "time",
    "model",
    "id",
    "data",
    NULL
};

r_device akhan_100F14 = {
    .name          = "Akhan 100F14 remote keyless entry",
    .modulation    = OOK_PULSE_PWM_PRECISE,
    .short_limit   = 316,
    .long_limit    = 1020,
    .reset_limit   = 1800,
    .sync_width    = 0,
    .tolerance     = 80, // us
    .json_callback = &akhan_rke_callback,
    .disabled      = 0,
    .demod_arg     = 0,
};
