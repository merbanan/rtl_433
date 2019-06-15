/* Opus/Imagintronix XT300 Soil Moisture Sensor
 *
 * Aslo called XH300 sometimes, this seems to be the associated display name
 *
 * https://www.plantcaretools.com/product/wireless-moisture-monitor/
 *
 * Data is transmitted with 6 bytes row:
 * 
 +.  0. 1. 2. 3. 4. 5
 *  FF ID SM TT ?? CC
 * 
 * FF: initial preamble
 * ID: 0101 01ID
 * SM: soil moisure (decimal 05 -> 99 %)
 * TT: temperature °C + 40°C (decimal)
 * ??: always FF... maybe spare bytes
 * CC: check sum (simple sum) except 0xFF preamble
  * 
 */

#include "decoder.h"

static int opus_xt300_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = 0;
    int row;
    int chk;
    uint8_t *b;
    int channel, temp, moisture;
    data_t *data;

    for (row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] != 48) {
            continue;
        }
        b = bitbuffer->bb[row];
        if (b[0] != 0xFF && ((b[1] | 0x1) & 0xFD) == 0x55) {
            continue;
        }
        chk = add_bytes(b + 1, 4); // sum bytes 1-4
        chk = chk & 0xFF;
        if (chk != 0 && chk != b[5] ) {
            continue;
        }

        channel  = (b[1] & 0x03);
        temp     = b[3] - 40;
        moisture =  b[2];

        data = data_make(
            "model",            "",             DATA_STRING, "Opus-XT300",
            "channel",          "Channel",      DATA_INT,    channel,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%d C", DATA_INT, temp,
            "moisture",         "Moisture",     DATA_FORMAT, "%d %%", DATA_INT, moisture,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
        decoder_output_data(decoder, data);
        ret++;
    }
    return ret;
}

static char *output_fields[] = {
    "model",
    "channel",
    "temperature_C",
    "moisture",
    NULL
};


r_device opus_xt300 = {
    .name           = "Opus/Imagintronix XT300 Soil Moisture",
    .modulation     = OOK_PULSE_PWM,
    .short_width    = 544,
    .long_width     = 932,
    .gap_limit      = 10000,
    .reset_limit    = 31000,
    .decode_fn      = &opus_xt300_callback,
    .disabled       = 0,
    .fields         = output_fields
};
