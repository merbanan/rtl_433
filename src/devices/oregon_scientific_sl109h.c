//some hints from http://www.osengr.org/WxShield/Downloads/OregonScientific-RF-Protocols-II.pdf

#include "decoder.h"

#define SL109H_MESSAGE_LENGTH 38
#define CHECKSUM_BYTE_COUNT 4
#define CHECKSUM_START_POSITION 6
#define HUMIDITY_START_POSITION 6
#define HUMIDITY_BIT_COUNT 8
#define TEMPERATURE_START_POSITION 14
#define TEMPERATURE_BIT_COUNT 12
#define ID_START_POSITION 30

static uint8_t calculate_humidity(bitbuffer_t *bitbuffer, unsigned row_index)
{
    int units_digit, tens_digit;

    uint8_t out[1] = {0};
    bitbuffer_extract_bytes(bitbuffer, row_index, HUMIDITY_START_POSITION, out, HUMIDITY_BIT_COUNT);

    units_digit = out[0] & 0x0f;

    tens_digit = (out[0] & 0xf0) >> 4;

    return 10 * tens_digit + units_digit;
}

static int calculate_centigrade_decidegrees(bitbuffer_t *bitbuffer, unsigned row_index)
{
    unsigned int decidegrees = 0;

    uint8_t out[2] = {0};
    bitbuffer_extract_bytes(bitbuffer, row_index, TEMPERATURE_START_POSITION, out, TEMPERATURE_BIT_COUNT);

    if(out[0] & 0x80) decidegrees = (~decidegrees << TEMPERATURE_BIT_COUNT);

    decidegrees |= (out[0] << 4) | ((out[1] & 0xf0) >> 4);

    return decidegrees;
}

static uint8_t get_channel_bits(uint8_t first_byte)
{
    return (first_byte & 0x0c) >> 2;
}
static int calculate_channel(uint8_t channel_bits)
{
    return (channel_bits % 3) ? channel_bits : 3;
}

//rolling code: changes when thermometer reset button is pushed/battery is changed
static uint8_t get_id(bitbuffer_t *bitbuffer, unsigned row_index)
{
    uint8_t out[1] = {0};
    bitbuffer_extract_bytes(bitbuffer, row_index, ID_START_POSITION, out, SL109H_MESSAGE_LENGTH - ID_START_POSITION);

    return out[0];
}

//there may be more specific information here; not currently certain what information is encoded here
static int get_status(uint8_t fourth_byte)
{
    return (fourth_byte & 0x3C) >> 2;
}

static int calculate_checksum(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row_index, int channel)
{
    uint8_t calculated_checksum, actual_checksum;
    uint8_t sum = 0;
    int  actual_expected_comparison;

    uint8_t out[CHECKSUM_BYTE_COUNT] = {0};
    bitbuffer_extract_bytes(bitbuffer, row_index, CHECKSUM_START_POSITION, out, SL109H_MESSAGE_LENGTH - CHECKSUM_START_POSITION);

    for(int i = 0; i < CHECKSUM_BYTE_COUNT; i++) sum += (out[i] & 0x0f) + (out[i] >> 4);

    calculated_checksum = ((sum + channel) % 16);
    actual_checksum     = bitbuffer->bb[row_index][0] >> 4;
    actual_expected_comparison = (calculated_checksum == actual_checksum);

    if(decoder->verbose & !actual_expected_comparison) {
        fprintf(stderr, "Checksum error in Oregon Scientific SL109H message.  Expected: %01x  Calculated: %01x\n", actual_checksum, calculated_checksum);
        fprintf(stderr, "Message: ");
        bitbuffer_print(bitbuffer);
    }

    return actual_expected_comparison;
}


static int oregon_scientific_sl109h_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    uint8_t *msg;
    uint8_t channel_bits;

    float temp_c;
    uint8_t humidity;
    int channel;
    uint8_t id;
    int status;

    for(int row_index = 0; row_index < bitbuffer->num_rows; row_index++) {
        msg = bitbuffer->bb[row_index];

        channel_bits = get_channel_bits(msg[0]);

        if( !((bitbuffer->bits_per_row[row_index] == SL109H_MESSAGE_LENGTH)
              && calculate_checksum(decoder, bitbuffer, row_index, channel_bits)) ) continue;


        temp_c = calculate_centigrade_decidegrees(bitbuffer, row_index) / 10.0;

        humidity = calculate_humidity(bitbuffer, row_index);

        id = get_id(bitbuffer, row_index);

        channel = calculate_channel(channel_bits);

        status = get_status(msg[3]);

        data = data_make(
                         "model",         "Model",                              DATA_STRING,    _X("Oregon-SL109H","Oregon Scientific SL109H"),
                         "id",            "Id",                                 DATA_INT,    id,
                         "channel",       "Channel",                            DATA_INT,    channel,
                         "temperature_C", "Celsius",	DATA_FORMAT, "%.02f C", DATA_DOUBLE, temp_c,
                         "humidity",      "Humidity",	DATA_FORMAT, "%u %%",   DATA_INT,    humidity,
                         "status",        "Status",                             DATA_INT,    status,
                         "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
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
    "status",
    "temperature_C",
    "humidity",
    "mic",
    NULL
};

r_device oregon_scientific_sl109h = {
    .name           = "Oregon Scientific SL109H Remote Thermal Hygro Sensor",
    .modulation     = OOK_PULSE_PPM,
    .short_width    = 2000,
    .long_width     = 4000,
    .gap_limit      = 5000,
    .reset_limit    = 10000, // packet gap is 8900
    .decode_fn      = &oregon_scientific_sl109h_callback,
    .disabled       = 0,
    .fields         = output_fields
};
