/** @file
    LaCrosse/StarMétéo/Conrad TX35 protocol.
*/
/**
Generic decoder for LaCrosse "IT+" (instant transmission) protocol.
Param device29or35 contain "29" or "35" depending of the device.

LaCrosse/StarMétéo/Conrad TX35DTH-IT, TFA Dostmann 30.3155     Temperature/Humidity Sensors.
LaCrosse/StarMétéo/Conrad TX29-IT                              Temperature Sensors.
Tune to 868240000Hz

Protocol
========
Example Data (gfile-tx29.data : https://github.com/merbanan/rtl_433_tests/tree/master/tests/lacrosse/06)
~~~
   a    a    2    d    d    4    9    2    8    4    4    8    6    a    e    c
Bits :
1010 1010 0010 1101 1101 0100 1001 0010 1000 0100 0100 1000 0110 1010 1110 1100
Bytes num :
----1---- ----2---- ----3---- ----4---- ----5---- ----6---- ----7---- ----8----
~~~~~~~~~ 1st byte
preamble, always "0xaa"
          ~~~~~~~~~~~~~~~~~~~ bytes 2 and 3
brand identifier, always 0x2dd4
                              ~~~~ 1st nibble of bytes 4
datalength (always 9) in nibble, including this field and crc
                                   ~~~~ ~~ 2nd nibble of bytes 4 and 1st and 2nd bits of byte 5
Random device id (6 bits)
                                          ~ 3rd bits of byte 5
new battery indicator
                                           ~ 4th bits of byte 5
unknown, unused
                                             ~~~~ ~~~~ ~~~~ 2nd nibble of byte 5 and byte 6
temperature, in bcd *10 +40
                                                            ~ 1st bit of byte 7
weak battery
                                                             ~~~ ~~~~ 2-8 bits of byte 7
humidity, in%. If == 0x6a : no humidity sensor
                                                                      ~~~~ ~~~~ byte 8
crc8 of bytes
~~~

Developer's comments
====================
I have noticed that depending of the device, the message received has different length.
It seems some sensor send a long preamble (33 bits, 0 / 1 alternated), and some send only
one byte as the preamble. I own 3 sensors TX29, and two of them send a long preamble.
So this decoder synchronize on the 0xaa 0x2d 0xd4 preamble, so many 0xaa can occurs before.
Also, I added 0x9 in the preamble (the weather data length), because this decoder only handle
this type of message.
TX29 and TX35 share the same protocol, but pulse are different length, thus this decoder
handle the two signal and we use two r_device struct (only differing by the pulse width)

How to make a decoder : https://enavarro.me/ajouter-un-decodeur-ask-a-rtl_433.html
*/

#include "decoder.h"

#define LACROSSE_TX29_NOHUMIDSENSOR  0x6a // Sensor do not support humidity
#define LACROSSE_TX35_CRC_POLY       0x31
#define LACROSSE_TX35_CRC_INIT       0x00
#define LACROSSE_TX29_MODEL          29 // Model number
#define LACROSSE_TX35_MODEL          35

static int lacrosse_it(r_device *decoder, bitbuffer_t *bitbuffer, uint8_t device29or35)
{
    data_t *data;
    int brow;
    uint8_t out[8];
    int r_crc, c_crc;
    int sensor_id, newbatt, battery_low;
    int humidity;
    float temp_c;
    int events = 0;

    static const uint8_t preamble[] = {
            0xaa, // preamble
            0x2d, // brand identifier
            0xd4, // brand identifier
            0x90, // data length (this decoder work only with data length of 9, so we hardcode it on the preamble)
    };

    for (brow = 0; brow < bitbuffer->num_rows; ++brow) {
        // Validate message and reject it as fast as possible : check for preamble
        unsigned int start_pos = bitbuffer_search(bitbuffer, brow, 0, preamble, 28);
        if (start_pos == bitbuffer->bits_per_row[brow])
            continue; // no preamble detected, move to the next row
        if (decoder->verbose)
            fprintf(stderr, "LaCrosse TX29/35 detected, buffer is %d bits length, device is TX%d\n", bitbuffer->bits_per_row[brow], device29or35);
        // remove preamble and keep only 64 bits
        bitbuffer_extract_bytes(bitbuffer, brow, start_pos, out, 64);

        /*
         * Check message integrity (CRC/Checksum/parity)
         * Normally, it is computed on the whole message, from byte 0 (preamble) to byte 6,
         * but preamble is always the same, so we can speed the process by doing a crc check
         * only on byte 3,4,5,6
         */
        r_crc = out[7];
        c_crc = crc8(&out[3], 4, LACROSSE_TX35_CRC_POLY, LACROSSE_TX35_CRC_INIT);
        if (r_crc != c_crc) {
            if (decoder->verbose)
                fprintf(stderr, "LaCrosse TX29/35 bad CRC: calculated %02x, received %02x\n", c_crc, r_crc);
            // reject row
            continue;
        }

        /*
         * Now that message "envelope" has been validated,
         * start parsing data.
         */
        sensor_id   = ((out[3] & 0x0f) << 2) | (out[4] >> 6);
        temp_c      = 10.0 * (out[4] & 0x0f) + 1.0 * ((out[5] >> 4) & 0x0f) + 0.1 * (out[5] & 0x0f) - 40.0;
        newbatt     = (out[4] >> 5) & 1;
        battery_low = out[6] >> 7;
        humidity    = out[6] & 0x7f;
        if (humidity == LACROSSE_TX29_NOHUMIDSENSOR) {
            data = data_make(
                    "brand", "", DATA_STRING, "LaCrosse",
                    "model", "", DATA_STRING, (device29or35 == 29 ? _X("LaCrosse-TX29IT","TX29-IT") : _X("LaCrosse-TX35DTHIT","TX35DTH-IT")),
                    "id", "", DATA_INT, sensor_id,
                    "battery", "Battery", DATA_STRING, battery_low ? "LOW" : "OK",
                    "newbattery", "NewBattery", DATA_INT, newbatt,
                    "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "mic", "Integrity", DATA_STRING, "CRC",
                    NULL);
        } else {
            data = data_make(
                    "brand", "", DATA_STRING, "LaCrosse",
                    "model", "", DATA_STRING, (device29or35 == 29 ? _X("LaCrosse-TX29IT","TX29-IT") : _X("LaCrosse-TX35DTHIT","TX35DTH-IT")),
                    "id", "", DATA_INT, sensor_id,
                    "battery", "Battery", DATA_STRING, battery_low ? "LOW" : "OK",
                    "newbattery", "NewBattery", DATA_INT, newbatt,
                    "temperature_C", "Temperature", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                    "humidity", "Humidity", DATA_FORMAT, "%u %%", DATA_INT, humidity,
                    "mic", "Integrity", DATA_STRING, "CRC",
                    NULL);
        }
        // humidity = -1; // The TX29-IT sensor do not have humidity. It is replaced by a special value

        decoder_output_data(decoder, data);
        events++;
    }
    return events;
}

/**
 ** Wrapper for the TX29 device
 **/
static int lacrossetx29_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    return lacrosse_it(decoder, bitbuffer, LACROSSE_TX29_MODEL);
}

/**
 ** Wrapper for the TX35 device
 **/
static int lacrossetx35_callback(r_device *decoder, bitbuffer_t *bitbuffer) {
    return lacrosse_it(decoder, bitbuffer, LACROSSE_TX35_MODEL);
}

static char *output_fields[] = {
    "brand",
    "model",
    "id",
    "battery",
    "newbattery",
    "temperature_C",
    "humidity",
    "mic",
    NULL
};

// Receiver for the TX29 device
r_device lacrosse_tx29 = {
    .name           = "LaCrosse TX29IT Temperature sensor",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 55,
    .long_width     = 55,
    .reset_limit    = 4000,
    .decode_fn      = &lacrossetx29_callback,
    .disabled       = 0,
    .fields         = output_fields,
};

// Receiver for the TX35 device
r_device lacrosse_tx35 = {
    .name           = "LaCrosse TX35DTH-IT, TFA Dostmann 30.3155 Temperature/Humidity sensor",
    .modulation     = FSK_PULSE_PCM,
    .short_width    = 105,
    .long_width     = 105,
    .reset_limit    = 4000,
    .decode_fn      = &lacrossetx35_callback,
    .disabled       = 0,
    .fields         = output_fields,
};
