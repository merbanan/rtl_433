// Generated from lacrosse_tx31u.py
/** @file
    LaCrosse TX31U-IT protocol.

    Decoder for LaCrosse transmitter provided with the WS-1910TWC-IT product.
    Branded with "The Weather Channel" logo.

    FCC ID: OMO-TX22U
    FSK_PCM @915 MHz, 116usec/bit

    Protocol
    --------

    This transmitter uses a variable length protocol that includes 1-5 measurements
    of 2 bytes each.  The first nibble of each measurement identifies the sensor.

        Sensor      Code    Encoding
        TEMP          0       BCD tenths of a degree C plus 400 offset.
                                  EX: 0x0653 is 25.3 degrees C
        HUMID         1       BCD % relative humidity.
                                  EX: 0x1068 is 68%
        UNKNOWN       2       This is probably reserved for a rain gauge (TX32U-IT) - NOT TESTED
        WIND_AVG_DIR  3       Wind direction and decimal time averaged wind speed in m/sec.
                                  First nibble is direction in units of 22.5 degrees.
        WIND_MAX      4       Decimal maximum wind speed in m/sec during last reporting interval.
                                  First nibble is 0x1 if wind sensor input is lost.

    Data layout:

           a    a    a    a    2    d    d    4    a    2    e    5    0    6    5    3    c    0
        Bits :
        1010 1010 1010 1010 0010 1101 1101 0100 1010 0010 1110 0101 0000 0110 0101 0011 1100 0000
        ~~~~~~~~~~~~~~~~~~~ 2 bytes preamble (0xaaaa)
                            ~~~~~~~~~~~~~~~~~~~ bytes 3 and 4 sync word of 0x2dd4
        sensor model (always 0xa)               ~~~~
        Random device id (6 bits)                    ~~~~ ~~
        Initial training mode (all sensors report)          ~
        no external sensor detected                          ~
        low battery indication                                 ~
        count of sensors reporting (1 to 5)                     ~~~
        sensor code                                                 ~~~~
        sensor reading (meaning varies, see above)                       ~~~~ ~~~~ ~~~~
        ---
        --- repeat sensor code:reading as specified in count value above
        ---
        crc8 (poly 0x31 init 0x00) of bytes 5 thru (N-1)                                ~~~~ ~~~~

    The WS-1910TWC-IT does not have a rain gauge or wind direction vane.  The readings output here
    are inferred from the output data, and correlating it with other similar Lacrosse devices.
    These readings have not been tested.
*/

#include "decoder.h"
#include "lacrosse_tx31u.h"

/** @fn static int lacrosse_tx31u_decode(r_device *decoder, bitbuffer_t *bitbuffer)
    LaCrosse TX31U-IT protocol.

    Decoder for LaCrosse transmitter provided with the WS-1910TWC-IT product.
    Branded with "The Weather Channel" logo.

    FCC ID: OMO-TX22U
    FSK_PCM @915 MHz, 116usec/bit

    Protocol
    --------

    This transmitter uses a variable length protocol that includes 1-5 measurements
    of 2 bytes each.  The first nibble of each measurement identifies the sensor.

        Sensor      Code    Encoding
        TEMP          0       BCD tenths of a degree C plus 400 offset.
                                  EX: 0x0653 is 25.3 degrees C
        HUMID         1       BCD % relative humidity.
                                  EX: 0x1068 is 68%
        UNKNOWN       2       This is probably reserved for a rain gauge (TX32U-IT) - NOT TESTED
        WIND_AVG_DIR  3       Wind direction and decimal time averaged wind speed in m/sec.
                                  First nibble is direction in units of 22.5 degrees.
        WIND_MAX      4       Decimal maximum wind speed in m/sec during last reporting interval.
                                  First nibble is 0x1 if wind sensor input is lost.

    Data layout:

           a    a    a    a    2    d    d    4    a    2    e    5    0    6    5    3    c    0
        Bits :
        1010 1010 1010 1010 0010 1101 1101 0100 1010 0010 1110 0101 0000 0110 0101 0011 1100 0000
        ~~~~~~~~~~~~~~~~~~~ 2 bytes preamble (0xaaaa)
                            ~~~~~~~~~~~~~~~~~~~ bytes 3 and 4 sync word of 0x2dd4
        sensor model (always 0xa)               ~~~~
        Random device id (6 bits)                    ~~~~ ~~
        Initial training mode (all sensors report)          ~
        no external sensor detected                          ~
        low battery indication                                 ~
        count of sensors reporting (1 to 5)                     ~~~
        sensor code                                                 ~~~~
        sensor reading (meaning varies, see above)                       ~~~~ ~~~~ ~~~~
        ---
        --- repeat sensor code:reading as specified in count value above
        ---
        crc8 (poly 0x31 init 0x00) of bytes 5 thru (N-1)                                ~~~~ ~~~~

    The WS-1910TWC-IT does not have a rain gauge or wind direction vane.  The readings output here
    are inferred from the output data, and correlating it with other similar Lacrosse devices.
    These readings have not been tested.
*/
static int lacrosse_tx31u_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble[] = {0xaa, 0xaa, 0x2d, 0xd4};
    if (bitbuffer->num_rows < 1)
        return DECODE_ABORT_LENGTH;
    unsigned offset = bitbuffer_search(bitbuffer, 0, 0, preamble, 32);
    if (offset >= bitbuffer->bits_per_row[0])
        return DECODE_ABORT_EARLY;
    offset += 32;

    uint8_t b[19];
    bitbuffer_extract_bytes(bitbuffer, 0, offset, b, 152);

    if (bitrow_get_bits(b, 0, 4) != 0xa)
        return DECODE_FAIL_SANITY;
    int sensor_id = bitrow_get_bits(b, 4, 6);
    int training = bitrow_get_bits(b, 10, 1);
    int no_ext = bitrow_get_bits(b, 11, 1);
    int battery_low = bitrow_get_bits(b, 12, 1);
    int measurements = bitrow_get_bits(b, 13, 3);
    int readings_sensor_type[8];
    int readings_reading[8];
    int bit_pos = 16;
    for (int _i = 0; _i < measurements && _i < 8; _i++) {
        readings_sensor_type[_i] = bitrow_get_bits(b, bit_pos, 4);
        bit_pos += 4;
        readings_reading[_i] = bitrow_get_bits(b, bit_pos, 12);
        bit_pos += 12;
    }

    int crc = bitrow_get_bits(b, bit_pos, 8);
    bit_pos += 8;
    int vret_validate_crc = lacrosse_tx31u_validate_crc(b, measurements, crc);
    if (vret_validate_crc != 0)
        return vret_validate_crc;


    float temperature_c = lacrosse_tx31u_temperature_c(readings_sensor_type, readings_reading, measurements);
    int humidity = lacrosse_tx31u_humidity(readings_sensor_type, readings_reading, measurements);

    /* clang-format off */
    data_t *data = data_make(
        "model", "", DATA_STRING, "LaCrosse-TX31UIT",
        "sensor_id", "", DATA_INT, sensor_id,
        "training", "", DATA_INT, training,
        "no_ext", "", DATA_INT, no_ext,
        "battery_low", "", DATA_INT, battery_low,
        "measurements", "", DATA_INT, measurements,
        "crc", "", DATA_INT, crc,
        "temperature_c", "", DATA_DOUBLE, temperature_c,
        "humidity", "", DATA_INT, humidity,
        NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);
    return 1;
}

static char const *const output_fields[] = {
    "model",
    "sensor_id",
    "training",
    "no_ext",
    "battery_low",
    "measurements",
    "crc",
    "temperature_c",
    "humidity",
    NULL,
};

r_device const lacrosse_tx31u = {
    .name        = "LaCrosse TX31U-IT, The Weather Channel WS-1910TWC-IT",
    .modulation  = FSK_PULSE_PCM,
    .short_width = 116.0,
    .long_width  = 116.0,
    .reset_limit = 20000.0,
    .decode_fn   = &lacrosse_tx31u_decode,
    .fields      = output_fields,
};
