/* SwitchDoc Labs F016TH Thermo-Hygrometer
 * Modified by John Shovic
 * contributed by David Ediger
 * discovered by Ron C. Lewis
 *
 * The check is an LFSR Digest-8, gen 0x98, key 0x3e, init 0x64
 */

#include "decoder.h"

// three repeats without gap
// full preamble is 0x00145 (the last bits might not be fixed, e.g. 0x00146)
// and on decoding also 0xffd45
static const uint8_t preamble_pattern[2] = {0x01, 0x45}; // 12 bits
static const uint8_t preamble_inverted[2] = {0xfd, 0x45}; // 12 bits

static int
switchdoclabs_F016TH_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    uint8_t b[6];
    int modelNumber;
    int deviceID;
    int isBatteryLow;
    int channel;
    float temperature;
    int humidity;
    data_t *data;

    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, 6*8);

    uint8_t expected = b[5];
   
    uint8_t calculated = lfsr_digest8(b, 5, 0x98, 0x3e) ^ 0x64;
    int i;
    /* for (i=0; i< 5; i++)
    {
	    fprintf(stderr,"b(%d) =0x%02x %d\n",i, b[i], b[i]);
    } 
    */

    //	expected = calculated; // disable CRC
    //
    if (expected != calculated) {
        if (decoder->verbose) {
            fprintf(stderr, "Checksum error in SwitchDoc Labs F016TH message.    Expected: %02x    Calculated: %02x\n", expected, calculated);
            fprintf(stderr, "Message: ");
            bitrow_print(b, 48);
        }
        return 0;
    }
    
    modelNumber = b[0] & 0x0F;
    // fprintf(stderr, "modelnumber = 0x%02x %d\n", modelNumber, modelNumber);
    deviceID = b[1];
    // fprintf(stderr, "deviceid = 0x%02x %d\n", deviceID, deviceID);
    
    if (modelNumber != 5)
    	{
	    return 0;
	}
    isBatteryLow = (b[2] & 0x80) != 0; // if not zero, battery is low
    channel = ((b[2] & 0x70) >> 4) + 1;
    int temp_f = ((b[2] & 0x0f) << 8) | b[3];
    temperature = (temp_f - 400) / 10.0f;
    humidity = b[4];

    data = data_make(
            "model",          "",             DATA_STRING, "SwitchDocLabs-F016TH",
            "id",
            "House Code",   DATA_INT,    deviceID,
            "modelnumber",        "Model Number",      DATA_INT,    modelNumber,
            "channel",        "Channel",      DATA_INT,    channel,
            "battery",        "Battery",      DATA_STRING, isBatteryLow ? "Low" : "OK",
            "temperature_F",  "Temperature",  DATA_FORMAT, "%.1f F", DATA_DOUBLE, temperature,
            "humidity",       "Humidity",     DATA_FORMAT, "%u %%", DATA_INT, humidity,
            "mic",            "Integrity",    DATA_STRING, "CRC",
            NULL);
    decoder_output_data(decoder, data);

    return 1;
}

static int
switchdoclabs_F016TH_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int row;
    unsigned bitpos;
    int events = 0;

    for (row = 0; row < bitbuffer->num_rows; ++row) {
        bitpos = 0;
        // Find a preamble with enough bits after it that it could be a complete packet
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_pattern, 12)) + 8+6*8 <=
                bitbuffer->bits_per_row[row]) {
            events += switchdoclabs_F016TH_decode(decoder, bitbuffer, row, bitpos + 8);
            if (events) return events; // for now, break after first successful message
            bitpos += 16;
        }
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos,
                (const uint8_t *)&preamble_inverted, 12)) + 8+6*8 <=
                bitbuffer->bits_per_row[row]) {
            events += switchdoclabs_F016TH_decode(decoder, bitbuffer, row, bitpos + 8);
            if (events) return events; // for now, break after first successful message
            bitpos += 15;
        }
    }

    return events;
}

static char *output_fields[] = {
    "model",
    "device", // TODO: delete this
    "id",
    "channel",
    "battery",
    "temperature_F",
    "humidity",
    "mic",
    NULL
};

r_device switchdoclabs_F016TH = {
    .name          = "SwitchDoc Labs F016TH Temperature Humidity Sensor",
    .modulation    = OOK_PULSE_MANCHESTER_ZEROBIT,
    .short_width   = 500,
    .long_width    = 0, // not used
    .reset_limit   = 2400,
    .decode_fn     = &switchdoclabs_F016TH_callback,
    .disabled      = 0,
    .fields        = output_fields
};
