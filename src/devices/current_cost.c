#include "decoder.h"

static int current_cost_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    bitbuffer_invert(bitbuffer);
    bitrow_t *bb = bitbuffer->bb;
    uint8_t *b = bb[0];

    data_t *data;

    uint8_t init_pattern[] = {
        0xcc, //8
        0xcc, //16
        0xcc, //24
        0xce, //32
        0x91, //40
        0x5d, //45 (! last 3 bits is not init)
    };
    unsigned int start_pos = bitbuffer_search(bitbuffer, 0, 0, init_pattern, 45);

    if(start_pos == bitbuffer->bits_per_row[0]){
        return 0;
    }
    start_pos += 45;

    bitbuffer_t packet_bits = {0};

    start_pos = bitbuffer_manchester_decode(bitbuffer, 0, start_pos, &packet_bits, 0);

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
                "model",         "",              DATA_STRING, _X("CurrentCost-TX","CurrentCost TX"), //TODO: it may have different CC Model ? any ref ?
                //"rc",            "Rolling Code",  DATA_INT, rc, //TODO: add rolling code b[1] ? test needed
                _X("id","dev_id"),       "Device Id",     DATA_FORMAT, "%d", DATA_INT, device_id,
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
       uint32_t c_impulse = (unsigned)packet[4] << 24 | packet[5] <<16 | packet[6] <<8 | packet[7] ;
       data = data_make(
               "model",        "",              DATA_STRING, _X("CurrentCost-Counter","CurrentCost Counter"), //TODO: it may have different CC Model ? any ref ?
               _X("id","dev_id"),       "Device Id",     DATA_FORMAT, "%d", DATA_INT, device_id,
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
    "dev_id", // TODO: delete this
    "id",
    "power0",
    "power1",
    "power2",
    NULL
};

r_device current_cost = {
    .name           = "CurrentCost Current Sensor",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 250,
    .long_width     = 250, // NRZ
    .reset_limit    = 8000,
    .decode_fn      = &current_cost_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
