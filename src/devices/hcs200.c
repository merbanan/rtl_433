/*
 * Microchip HCS200 KeeLoq Code Hopping Encoder based remotes
 *
 * (c) 2019, 667bdrm
 *
 * Datasheet: DS40138C http://ww1.microchip.com/downloads/en/DeviceDoc/40138c.pdf
 *
 * rtl_433 -R 0 -X 'n=name,m=OOK_PWM,s=370,l=772,r=14000,g=4000,t=152,y=0,preamble={12}0xfff'
 */
#include "decoder.h"

static int hcs200_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *b = bitbuffer->bb[0];
    char encrypted[9];
    int i;
	uint32_t serial;
	char serial_str[9];

    /* Reject codes of wrong length */
    if (78 != bitbuffer->bits_per_row[0])
      return 0;
	
	for (i = 1 ; i < 10; i++) {
	   b[i] = ( b[i] << 4 ) | ((b[i+1] & 0xf0) >> 4);
    }
	
	serial = (((b[5] << 24) | (b[6] << 16) | (b[7] << 8) | (b[8] & 0x0F)) >> 4) & 0x0fffffff;
	
	sprintf(encrypted, "%04X", (b[4] | (b[3] << 8) | (b[2] << 16) | (b[1] << 24)));
    sprintf(serial_str, "%04X", serial);

    data = data_make(
        "model",     "",     DATA_STRING,    _X("HCS200","Microchip HCS200"),
        "serial",    "",     DATA_STRING,    serial_str,
        "encrypted", "",     DATA_STRING,    encrypted,
        "button1",    "",    DATA_STRING,     ((b[8] & 0x04) == 0x04) ? "ON" : "OFF",
		"button2",    "",    DATA_STRING,     ((b[8] & 0x02) == 0x02) ? "ON" : "OFF",
		"button3",    "",    DATA_STRING,     ((b[8] & 0x09) == 0x09) ? "ON" : "OFF",
		"button4",    "",    DATA_STRING,     ((b[8] & 0x06) == 0x06) ? "ON" : "OFF",
		"misc",       "",    DATA_STRING,     (b[8] == 0x0F) ? "ALL_PRESSED" : "",
        "battery",   "",     DATA_STRING,    (((b[9] >> 4) & 0x08) == 0x08) ? "LOW" : "OK",
        NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "serial",
    "encrypted",
    "button1",
    "button2",
    "button3",
	"button4",
    "misc",
    "battery",
    NULL
};

r_device hcs200 = {
    .name           = "Microchip HCS200",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 370,
    .long_width     = 772,
    .gap_limit      = 4000,
    .reset_limit    = 14000,
    .sync_width     = 0,    // No sync bit used
    .tolerance      = 152,  // us
    .decode_fn      = &hcs200_callback,
    .disabled       = 0,
    .fields         = output_fields
};
