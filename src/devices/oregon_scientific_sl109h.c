/** @file
    Oregon Scientific SL109H decoder.
*/
/**
Oregon Scientific SL109H decoder.

Data layout (bits):

    AAAA CC HHHH HHHH TTTT TTTT TTTT SSSS IIII IIII

- A: 4 bit checksum (add)
- C: 2 bit channel number
- H: 8 bit BCD humidity
- T: 12 bit signed temperature scaled by 10
- S: 4 bit status, unknown
- I: 8 bit a random id that is generated when the sensor starts

S.a. http://www.osengr.org/WxShield/Downloads/OregonScientific-RF-Protocols-II.pdf
*/

#include "decoder.h"

static int oregon_scientific_sl109h_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *msg;
    uint8_t b[5];

    uint8_t sum, chk;
    int channel;
    uint8_t humidity;
    int temp_raw;
    float temp_c;
    int status;
    uint8_t id;

    for (int row_index = 0; row_index < bitbuffer->num_rows; row_index++) {
        if (bitbuffer->bits_per_row[row_index] != 38) // expected length is 38 bit
            continue; // DECODE_ABORT_LENGTH

        msg = bitbuffer->bb[row_index];

        // No need to decode/extract values for simple test
        // check id channel temperature humidity value not zero
        if (!msg[0] && !msg[1] && !msg[2] && !msg[3]) {
            decoder_log(decoder, 2, __func__, "DECODE_FAIL_SANITY data all 0x00");
            continue; // DECODE_FAIL_SANITY
        }

        chk = msg[0] >> 4;

        // align the channel "half nibble"
        bitbuffer_extract_bytes(bitbuffer, row_index, 2, b, 36);
        b[0] &= 0x3f;

        // Prevent false positives from 'allzero'
        // reject if Checksum channelhumidity and temperature are all zero
        // No need to decode/extract values for simple test
        if (chk == 0 && b[0] == 0 && b[1] == 0 && b[2] == 0)
            continue; // DECODE_FAIL_SANITY

        sum = add_nibbles(b, 5) & 0xf;
        if (sum != chk) {
            decoder_logf_bitbuffer(decoder, 2, __func__, bitbuffer, "Checksum error. Expected: %01x Calculated: %01x", chk, sum);
            continue; // DECODE_FAIL_MIC
        }

        channel = b[0] >> 4;
        channel = (channel % 3) ? channel : 3;

        humidity = 10 * (b[0] & 0x0f) + (b[1] >> 4);

        temp_raw = (int16_t)((b[1] & 0x0f) << 12) | (b[2] << 4); // uses sign-extend
        temp_c   = (temp_raw >> 4) * 0.1f;

        // reduce false positives by checking specified sensor range, this isn't great...
        if (temp_c < -20 || temp_c > 60) {
            decoder_logf(decoder, 2, __func__, "temperature sanity check failed: %.1f C", temp_c);
            return DECODE_FAIL_SANITY;
        }

        // there may be more specific information here; not currently certain what information is encoded here
        status = (b[3] >> 4);

        // changes when thermometer reset button is pushed/battery is changed
        id = ((b[3] & 0x0f) << 4) | (b[4] >> 4);

        /* clang-format off */
        data = data_make(
                "model",            "Model",                                DATA_STRING, "Oregon-SL109H",
                "id",               "Id",                                   DATA_INT,    id,
                "channel",          "Channel",                              DATA_INT,    channel,
                "temperature_C",    "Celsius",      DATA_FORMAT, "%.1f C",  DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",     DATA_FORMAT, "%u %%",   DATA_INT,    humidity,
                "status",           "Status",                               DATA_INT,    status,
                "mic",              "Integrity",                            DATA_STRING, "CHECKSUM",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    return 0;
}

static char *output_fields[] = {
        "model",
        "id",
        "channel",
        "status",
        "temperature_C",
        "humidity",
        "mic",
        NULL,
};

r_device oregon_scientific_sl109h = {
        .name        = "Oregon Scientific SL109H Remote Thermal Hygro Sensor",
        .modulation  = OOK_PULSE_PPM,
        .short_width = 2000,
        .long_width  = 4000,
        .gap_limit   = 5000,
        .reset_limit = 10000, // packet gap is 8900
        .decode_fn   = &oregon_scientific_sl109h_callback,
        .fields      = output_fields,
};
