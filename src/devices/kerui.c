/* Kerui PIR sensor
 *
 * Such as
 * http://www.ebay.co.uk/sch/i.html?_from=R40&_trksid=p2050601.m570.l1313.TR0.TRC0.H0.Xkerui+pir.TRS0&_nkw=kerui+pir&_sacat=0
 *
 * also tested with:
 *   KERUI D026 433MHz Wireless Door Window Magnetic Sensor Dedector Alarm System
 *   (open / close / tamper)
 *
 * Note: simple 24 bit fixed ID protocol (x1527 style) and should be handled by the flex decoder.
 */

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"
#include "data.h"

static int kerui_callback(bitbuffer_t *bitbuffer) {
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
        case 0xa: cmd_str = "0xa (PIR)"; break;
        case 0xe: cmd_str = "0xa (open)"; break;
        case 0x7: cmd_str = "0xa (close)"; break;
        case 0xb: cmd_str = "0xa (tamper)"; break;
        default:  cmd_str = NULL; break;
    }

    if (!cmd_str)
        return 0;

    local_time_str(0, time_str);
    data = data_make(
            "time",     "",             DATA_STRING, time_str,
            "model",    "",             DATA_STRING, "Kerui PIR Sensor",
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

r_device kerui = {
    .name          = "Kerui PIR Sensor",
    .modulation    = OOK_PULSE_PWM_PRECISE,
    .short_limit   = 316,
    .long_limit    = 1020,
    .reset_limit   = 1800,
    .sync_width    = 0,
    .tolerance     = 80, // us
    .json_callback = &kerui_callback,
    .disabled      = 0,
    .demod_arg     = 0,
};
