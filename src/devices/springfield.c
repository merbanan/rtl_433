/* Springfield PreciseTemp Wireless Temperature and Soil Moisture Station
 * http://www.amazon.com/Springfield-Digital-Moisture-Meter-Freeze/dp/B0037BNHLS
 *
 * Data is transmitted in the following form:
 *
 * Nibble
 *  0-1   Power On ID
 *   2    Flags and Channel - BTCC
 *           B - Battery 0 = OK, 1 = LOW
 *           T - Transmit 0 = AUTO, 1 = MANUAL (TX Button Pushed)
 *          CC - Channel 00 = 1, 01 = 2, 10 = 3
 *  3-5   Temperature Celsius X 10 - 3 nibbles 2s complement
 *   6    Moisture Level - 0 - 10
 *   7    Checksum of nibbles 0 - 6 (simple xor of nibbles)
 *   8    Unknown
 *
 * Actually 37 bits for all but last transmission which is 36 bits
 */

#include "decoder.h"

static int springfield_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = 0;
    int row;
    int chk;
    uint8_t *b;
    int sid, battery, transmit, channel, temp;
    float temp_c;
    int moisture, uk1;
    data_t *data;
    unsigned tmpData;
    unsigned savData = 0;

    for (row = 0; row < bitbuffer->num_rows; row++) {
        if (bitbuffer->bits_per_row[row] != 36 && bitbuffer->bits_per_row[row] != 37)
            continue;
        b = bitbuffer->bb[row];
        tmpData = ((unsigned)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
        if (tmpData == 0xffffffff)
            continue; // prevent false positive checksum
        if (tmpData == savData)
            continue;
        savData = tmpData;

        chk = xor_bytes(b, 4); // sum nibble 0-7
        chk = (chk >> 4) ^ (chk & 0x0f); // fold to nibble
        if (chk != 0)
            continue;

        sid      = (b[0]);
        battery  = (b[1] >> 7) & 1;
        transmit = (b[1] >> 6) & 1;
        channel  = ((b[1] >> 4) & 0x03) + 1;
        temp     = (b[1] & 0x0f) << 8 | b[2];
        temp     = (int16_t)(temp << 4) >> 4; // sign extend
        temp_c    = temp * 0.1;
        moisture =  b[3] >> 4;
        uk1      =  b[4] >> 4;    /* unknown. */

        data = data_make(
                "model",            "",             DATA_STRING, _X("Springfield-Soil","Springfield Temperature & Moisture"),
                _X("id","sid"),              "SID",          DATA_INT,    sid,
                "channel",          "Channel",      DATA_INT,    channel,
                "battery",          "Battery",      DATA_STRING, battery ? "LOW" : "OK",
                "transmit",         "Transmit",     DATA_STRING, transmit ? "MANUAL" : "AUTO",
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.01f C", DATA_DOUBLE, temp_c,
                "moisture",         "Moisture",     DATA_INT,    moisture,
//                "uk1",            "uk1",          DATA_INT,    uk1,
                "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
                NULL);
        decoder_output_data(decoder, data);
        ret++;
    }
    return ret;
}

static char *output_fields[] = {
    "model",
    "sid", // TODO: delete this
    "id",
    "channel",
    "battery",
    "transmit",
    "temperature_C",
    "moisture",
    NULL
};

r_device springfield = {
    .name           = "Springfield Temperature and Soil Moisture",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 2000,
    .long_width     = 4000,
    .gap_limit      = 5000,
    .reset_limit    = 9200,
    .decode_fn      = &springfield_callback,
    .disabled       = 0,
    .fields         = output_fields
};
