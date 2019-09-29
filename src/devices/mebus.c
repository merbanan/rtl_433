#include "decoder.h"

static int mebus433_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    bitrow_t *bb = bitbuffer->bb;
    int16_t temp;
    int8_t  hum;
    uint8_t address;
    uint8_t channel;
    uint8_t battery;
    uint8_t unknown1;
    uint8_t unknown2;
    data_t *data;

    if (bb[0][0] == 0 && bb[1][4] !=0 && (bb[1][0] & 0x60) && bb[1][3]==bb[5][3] && bb[1][4] == bb[12][4]){

        address = bb[1][0] & 0x1f;

        channel = ((bb[1][1] & 0x30) >> 4) + 1;
        // Always 0?
        unknown1 = (bb[1][1] & 0x40) >> 6;
        battery = bb[1][1] & 0x80;

        // Upper 4 bits are stored in nibble 1, lower 8 bits are stored in nibble 2
        // upper 4 bits of nibble 1 are reserved for other usages.
        temp = (int16_t)((uint16_t)(bb[1][1] << 12) | bb[1][2] << 4);
        temp = temp >> 4;
        // lower 4 bits of nibble 3 and upper 4 bits of nibble 4 contains
        // humidity as decimal value
        hum  = (bb[1][3] << 4 | bb[1][4] >> 4);

        // Always 0b1111?
        unknown2 = (bb[1][3] & 0xf0) >> 4;

        data = data_make(
                "model",         "",            DATA_STRING, _X("Mebus-433","Mebus/433"),
                "id",            "Address",     DATA_INT, address,
                "channel",       "Channel",     DATA_INT, channel,
                "battery",       "Battery",     DATA_STRING, battery ? "OK" : "LOW",
                "unknown1",      "Unknown 1",   DATA_INT, unknown1,
                "unknown2",      "Unknown 2",   DATA_INT, unknown2,
                "temperature_C", "Temperature", DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp / 10.0,
                "humidity",      "Humidity",    DATA_FORMAT, "%u %%", DATA_INT, hum,
                NULL);
        decoder_output_data(decoder, data);


        return 1;
    }
    return 0;
}

static char *output_fields[] = {
    "model",
    "id",
    "channel",
    "battery",
    "unknown1",
    "unknown2",
    "temperature_C",
    "humidity",
    NULL
};

r_device mebus433 = {
    .name           = "Mebus 433",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 800, // guessed, no samples available
    .long_width     = 1600, // guessed, no samples available
    .gap_limit      = 2400,
    .reset_limit    = 6000,
    .decode_fn      = &mebus433_callback,
    .disabled       = 1, // add docs, tests, false positive checks and then reenable
    .fields         = output_fields
};
