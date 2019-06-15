/* Kerui PIR / Contact Sensor
 *
 * Such as
 * http://www.ebay.co.uk/sch/i.html?_from=R40&_trksid=p2050601.m570.l1313.TR0.TRC0.H0.Xkerui+pir.TRS0&_nkw=kerui+pir&_sacat=0
 *
 * also tested with:
 *   KERUI D026 Window Door Magnet Sensor Detector (433MHz) https://fccid.io/2AGNGKR-D026
 *   events: open / close / tamper / battery low (below 5V of 12V battery)
 *
 * Note: simple 24 bit fixed ID protocol (x1527 style) and should be handled by the flex decoder.
 */

#include "decoder.h"

static int kerui_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
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
        case 0xa: cmd_str = "motion"; break;
        case 0xe: cmd_str = "open"; break;
        case 0x7: cmd_str = "close"; break;
        case 0xb: cmd_str = "tamper"; break;
        case 0xf: cmd_str = "battery"; break;
        default:  cmd_str = NULL; break;
    }

    if (!cmd_str)
        return 0;

    data = data_make(
            "model",    "",               DATA_STRING, _X("Kerui-Security","Kerui Security"),
            "id",       "ID (20bit)",     DATA_FORMAT, "0x%x", DATA_INT, id,
            "cmd",      "Command (4bit)", DATA_FORMAT, "0x%x", DATA_INT, cmd,
            "state",    "State",          DATA_STRING, cmd_str,
            NULL);

    decoder_output_data(decoder, data);
    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "cmd",
    "state",
    NULL
};

r_device kerui = {
    .name          = "Kerui PIR / Contact Sensor",
    .modulation    = OOK_PULSE_PWM,
    .short_width   = 320,
    .long_width    = 960,
    .reset_limit   = 1100, // 9900,
    //.gap_limit     = 1100,
    .sync_width    = 480,
    .tolerance     = 80, // us
    .decode_fn     = &kerui_callback,
    .disabled      = 0,
    .fields         = output_fields,
};
