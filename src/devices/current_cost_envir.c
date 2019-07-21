/* CurrentCost EnviR Transmistter
 * Contributed by Neil Cowburn <git@neilcowburn.com>
 */

#include "decoder.h"

static int current_cost_envir_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    bitbuffer_invert(bitbuffer);
    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bb[0];

    data_t *data;
 
    // The EnviR transmits 0x55 0x55 0x55 0x55 0x2D 0xD4
    // which is a 4-byte preamble and a 2-byte syncword
    // The init pattern is inverted and left-shifted by 
    // 1 bit so that the decoder starts with a high bit. 
    uint8_t init_pattern[] = {
        0x55, 
        0x55, 
        0x55, 
        0x55, 
        0xa4, 
        0x57, 
    };

    unsigned int start_pos = bitbuffer_search(bitbuffer, 0, 0, init_pattern, 48);

    if(start_pos == bitbuffer->bits_per_row[0]){
        return 0;
    }

    // bitbuffer_search matches patterns starting on a high bit, but the EnviR protocol
    // starts with a low bit, so we have to adjust the offset by 1 to prevent the 
    // Manchester decoding from failing. This is perfectly safe though has the 47th bit 
    // is always 0 as it's the last bit of the 0x2DD4 syncword, i.e. 0010110111010100. 
    start_pos += 47;

    bitbuffer_t packet_bits = {0};

    start_pos = bitbuffer_manchester_decode(bitbuffer, 0, start_pos, &packet_bits, 0);

    // From here on in, everything is the same as the CurrentCost TX

    uint8_t *packet = packet_bits.bb[0];
    // Read data
    // Meter (packet[0] = 0000xxxx) bits 5 and 4 are "unknown", but always 0 to date.
    if(packet_bits.bits_per_row[0] >= 56 && ((packet[0] & 0xf0) == 0) ){
        uint16_t device_id = (packet[0] & 0x0f) << 8 | packet[1];
        uint16_t watt0 = 0;
        uint16_t watt1 = 0;
        uint16_t watt2 = 0;
        //Check the "Data valid indicator" bit is 1 before using the sensor values
        if((packet[2] & 0x80) == 128) { watt0 = (packet[2] & 0x7F) << 8 | packet[3] ; }
        if((packet[4] & 0x80) == 128) { watt1 = (packet[4] & 0x7F) << 8 | packet[5] ; }
        if((packet[6] & 0x80) == 128) { watt2 = (packet[6] & 0x7F) << 8 | packet[7] ; }
        data = data_make(
                "model",         "",              DATA_STRING, "CurrentCost-EnviR\tCurrentCost EnviR", //TODO: it may have different CC Model ? any ref ?
                //"rc",            "Rolling Code",  DATA_INT, rc, //TODO: add rolling code b[1] ? test needed
                "dev_id",       "Device Id",     DATA_FORMAT, "%d", DATA_INT, device_id,
                "power0",       "Power 0",       DATA_FORMAT, "%d W", DATA_INT, watt0,
                "power1",       "Power 1",       DATA_FORMAT, "%d W", DATA_INT, watt1,
                "power2",       "Power 2",       DATA_FORMAT, "%d W", DATA_INT, watt2,
                //"battery",       "Battery",       DATA_STRING, battery_low ? "LOW" : "OK", //TODO is there some low battery indicator ?
                NULL);
        decoder_output_data(decoder, data);
        return 1;
    }
    // Counter (packet[0] = 0100xxxx) bits 5 and 4 are "unknown", but always 0 to date.
    else if(packet_bits.bits_per_row[0] >= 56 && ((packet[0] & 0xf0) == 64) ){
       uint16_t device_id = (packet[0] & 0x0f) << 8 | packet[1];
       // packet[2] is "Apparently unused"
       uint16_t sensor_type = packet[3]; //Sensor type. Valid values are: 2-Electric, 3-Gas, 4-Water
       uint32_t c_impulse = packet[4] << 24 | packet[5] <<16 | packet[6] <<8 | packet[7] ;
       data = data_make(
               "model",        "",              DATA_STRING, "CurrentCost-EnviR-Counter\tCurrentCost EnviR Counter", //TODO: it may have different CC Model ? any ref ?
               "dev_id",       "Device Id",     DATA_FORMAT, "%d", DATA_INT, device_id,
               "sensor_type",  "Sensor Id",     DATA_FORMAT, "%d", DATA_INT, sensor_type, //Could "friendly name" this?
               //"counter",      "Counter",       DATA_FORMAT, "%d", DATA_INT, c_impulse,
               "power0",       "Counter",       DATA_FORMAT, "%d", DATA_INT, c_impulse,
               NULL);
       decoder_output_data(decoder, data);
       return 1;
    }

    return 0;
}

static char *output_fields[] = {
    "model",
    "dev_id",
    "power0",
    "power1",
    "power2",
    NULL
};

r_device current_cost_envir = {
    .name           = "CurrentCost EnviR Sensor",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 250,
    .long_width     = 250, // NRZ
    .reset_limit    = 8000,
    .decode_fn      = &current_cost_envir_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
