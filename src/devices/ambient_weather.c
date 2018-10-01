#include "pch.h"

// three repeats without gap
// full preamble is 0x00145
// and on decoding also 0xffd45
static const uint8_t preamble_pattern[2] = {0x01, 0x45}; // 16 bits
static const uint8_t preamble_inverted[2] = {0xfd, 0x45}; // 16 bits

static uint8_t
calculate_checksum(uint8_t *buff, int length)
{
    uint8_t mask = 0x7C;
    uint8_t checksum = 0x64;
    uint8_t data;
    int byteCnt;

    for (byteCnt=0; byteCnt < length; byteCnt++) {
        int bitCnt;
        data = buff[byteCnt];

        for (bitCnt = 7; bitCnt >= 0 ; bitCnt--) {
            uint8_t bit;

            // Rotate mask right
            bit = mask & 1;
            mask = (mask >> 1 ) | (mask << 7);
            if (bit) {
                mask ^= 0x18;
            }

            // XOR mask into checksum if data bit is 1
            if (data & 0x80) {
                checksum ^= mask;
            }
            data <<= 1;
        }
    }
    return checksum;
}

static int
ambient_weather_decode(bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[6];
    int deviceID;
    int isBatteryLow;
    int channel;
    float temperature;
    int humidity;
    char time_str[LOCAL_TIME_BUFLEN];
    data_t *data;

    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, 6*8);

    uint8_t expected = b[5];
    uint8_t calculated = calculate_checksum(b, 5);

    if (expected != calculated) {
        if (debug_output) {
            fprintf(stderr, "Checksum error in Ambient Weather message.    Expected: %02x    Calculated: %02x\n", expected, calculated);
            fprintf(stderr, "Message: ");
            for (int i=0; i < 6; i++)
                fprintf(stderr, "%02x ", b[i]);
            fprintf(stderr, "\n\n");
        }
        return 0;
    }

    deviceID = b[1];
    isBatteryLow = (b[2] & 0x80) != 0; // if not zero, battery is low
    channel = ((b[2] & 0x70) >> 4) + 1;
    int temp_f = ((b[2] & 0x0f) << 8) | b[3];
    temperature = (temp_f - 400) / 10.0f;
    humidity = b[4];

    local_time_str(0, time_str);
    data = data_make(
            "time",           "",             DATA_STRING, time_str,
            "model",          "",             DATA_STRING, "Ambient Weather F007TH Thermo-Hygrometer",
            "device",         "House Code",   DATA_INT,    deviceID,
            "channel",        "Channel",      DATA_INT,    channel,
            "battery",        "Battery",      DATA_STRING, isBatteryLow ? "Low" : "Ok",
            "temperature_F",  "Temperature",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature,
            "humidity",       "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",            "Integrity",    DATA_STRING, "CRC",
            NULL);
    data_acquired_handler(data);

    return 1;
}

static int
ambient_weather_callback(bitbuffer_t *bitbuffer)
{
    int row;
    unsigned bitpos;
    int events = 0;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_pattern, 16)) + 8+6*8 <=
                bitbuffer->bits_per_row[row]) {
            events += ambient_weather_decode(bitbuffer, row, bitpos + 8);
            if (events) return events; // for now, break after first successful message
            bitpos += 16;
        }
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_inverted, 16)) + 8+6*8 <=
                bitbuffer->bits_per_row[row]) {
            events += ambient_weather_decode(bitbuffer, row, bitpos + 8);
            if (events) return events; // for now, break after first successful message
            bitpos += 15;
        }
    }

    return events;
}

static char *output_fields[] = {
    "time",
    "model",
    "device",
    "channel",
    "battery",
    "temperature_F",
    "humidity",
    "mic",
    NULL
};

r_device ambient_weather = {
    .name          = "Ambient Weather Temperature Sensor",
    .modulation    = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_limit   = 500,
    .long_limit    = 0, // not used
    .reset_limit   = 2400,
    .json_callback = &ambient_weather_callback,
    .disabled      = 0,
    .demod_arg     = 0,
    .fields        = output_fields
};
